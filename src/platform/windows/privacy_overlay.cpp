/**
 * @file src/platform/windows/privacy_overlay.cpp
 * @brief Local black overlay excluded from Windows display capture.
 */

#include "privacy_overlay.h"

#include "src/logging.h"

#include <condition_variable>
#include <dwmapi.h>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <Windows.h>

namespace platf::privacy_overlay {
  namespace {
    constexpr wchar_t window_class[] = L"SunshinePrivacyOverlay";  ///< Private window class for local covers.
    constexpr UINT refresh_message = WM_APP + 1;  ///< Message used to rebuild windows after display changes.
    constexpr DWORD exclude_from_capture = 0x00000011;  ///< WDA_EXCLUDEFROMCAPTURE.

    /** @brief Synchronizes synchronous startup with the window message thread. */
    struct state_t {
      std::mutex mutex;
      std::condition_variable ready;
      std::thread worker;
      DWORD worker_id {};
      bool startup_complete {};
      bool startup_success {};
      std::function<void()> coverage_lost;
    } state;  ///< Current overlay thread state.

    /**
     * @brief Paint a black cover and request updates after display changes.
     * @param window Overlay window receiving the message.
     * @param message Windows message identifier.
     * @param wparam Message-specific value.
     * @param lparam Message-specific value.
     * @return Result for the processed message.
     */
    LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
      if (message == WM_PAINT) {
        PAINTSTRUCT paint {};
        auto dc = BeginPaint(window, &paint);
        RECT rect {};
        GetClientRect(window, &rect);
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(window, &paint);
        return 0;
      }
      if (message == WM_DISPLAYCHANGE || message == WM_DPICHANGED) {
        PostThreadMessageW(GetCurrentThreadId(), refresh_message, 0, 0);
      }
      return DefWindowProcW(window, message, wparam, lparam);
    }

    /**
     * @brief Append an active monitor rectangle to the enumeration result.
     * @param monitor Monitor being enumerated.
     * @param data Pointer to a vector of monitor rectangles.
     * @return TRUE while enumeration may continue.
     */
    BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
      MONITORINFO info {sizeof(info)};
      if (!GetMonitorInfoW(monitor, &info)) {
        return FALSE;
      }
      auto &rects = *reinterpret_cast<std::vector<RECT> *>(data);
      rects.push_back(info.rcMonitor);
      return TRUE;
    }

    /**
     * @brief Destroy windows owned by the current message thread.
     * @param windows Windows to destroy and clear.
     */
    void destroy_windows(std::vector<HWND> &windows) {
      for (auto window : windows) {
        DestroyWindow(window);
      }
      windows.clear();
    }

    /**
     * @brief Replace covers for all currently active monitors.
     * @param windows Existing covers, retained if creating replacements fails.
     * @return True when all monitor covers are ready.
     */
    bool refresh_windows(std::vector<HWND> &windows) {
      std::vector<RECT> monitors;
      if (!EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&monitors)) || monitors.empty()) {
        BOOST_LOG(error) << "Privacy overlay: unable to enumerate active monitors";
        return false;
      }

      std::vector<HWND> replacements;
      for (const auto &rect : monitors) {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        auto window = CreateWindowExW(
          WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
          window_class,
          L"",
          WS_POPUP,
          rect.left,
          rect.top,
          width,
          height,
          nullptr,
          nullptr,
          GetModuleHandleW(nullptr),
          nullptr
        );
        if (!window) {
          BOOST_LOG(error) << "Privacy overlay: CreateWindowExW failed: " << GetLastError();
          destroy_windows(replacements);
          return false;
        }
        replacements.push_back(window);

        DWORD affinity {};
        if (!SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA) ||
            !SetWindowPos(window, HWND_TOPMOST, rect.left, rect.top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW) ||
            !SetWindowDisplayAffinity(window, exclude_from_capture) ||
            !GetWindowDisplayAffinity(window, &affinity) || affinity != exclude_from_capture ||
            !IsWindowVisible(window)) {
          BOOST_LOG(error) << "Privacy overlay: unable to show a capture-excluded window: " << GetLastError();
          destroy_windows(replacements);
          return false;
        }
        UpdateWindow(window);
      }

      if (FAILED(DwmFlush())) {
        BOOST_LOG(error) << "Privacy overlay: DwmFlush failed";
        destroy_windows(replacements);
        return false;
      }

      destroy_windows(windows);
      windows = std::move(replacements);
      return true;
    }

    /** @brief Own and pump all overlay windows on a dedicated thread. */
    void run() {
      SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

      MSG message {};
      PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);

      WNDCLASSW window_type {};
      window_type.lpfnWndProc = window_proc;
      window_type.hInstance = GetModuleHandleW(nullptr);
      window_type.lpszClassName = window_class;
      const bool registered = RegisterClassW(&window_type) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;

      BOOL composition_enabled {};
      std::vector<HWND> windows;
      const bool success = registered && SUCCEEDED(DwmIsCompositionEnabled(&composition_enabled)) && composition_enabled &&
                           refresh_windows(windows);
      {
        std::lock_guard lock(state.mutex);
        state.worker_id = GetCurrentThreadId();
        state.startup_success = success;
        state.startup_complete = true;
      }
      state.ready.notify_one();
      if (!success) {
        destroy_windows(windows);
        return;
      }

      bool failure_reported = false;
      while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == refresh_message) {
          if (!refresh_windows(windows)) {
            BOOST_LOG(error) << "Privacy overlay: display change could not be covered";
            if (!failure_reported && state.coverage_lost) {
              failure_reported = true;
              state.coverage_lost();
            }
          }
        } else {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
      }
      destroy_windows(windows);
      std::lock_guard lock(state.mutex);
      state.startup_success = false;
    }
  }  // namespace

  bool start(std::function<void()> coverage_lost) {
    std::unique_lock lock(state.mutex);
    if (state.worker.joinable()) {
      state.ready.wait(lock, []() {
        return state.startup_complete;
      });
      return state.startup_success;
    }
    state.startup_complete = false;
    state.startup_success = false;
    state.coverage_lost = std::move(coverage_lost);
    try {
      state.worker = std::thread(run);
    } catch (const std::system_error &e) {
      BOOST_LOG(error) << "Privacy overlay: unable to start window thread: " << e.what();
      return false;
    }
    state.ready.wait(lock, []() {
      return state.startup_complete;
    });
    if (state.startup_success) {
      return true;
    }
    auto worker = std::move(state.worker);
    lock.unlock();
    worker.join();
    return false;
  }

  void stop() {
    std::unique_lock lock(state.mutex);
    if (!state.worker.joinable()) {
      return;
    }
    auto worker = std::move(state.worker);
    const auto worker_id = state.worker_id;
    lock.unlock();
    PostThreadMessageW(worker_id, WM_QUIT, 0, 0);
    worker.join();
    lock.lock();
    state.worker_id = 0;
    state.startup_success = false;
    state.coverage_lost = {};
  }
}  // namespace platf::privacy_overlay
