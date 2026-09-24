/**
 * @file src/platform/windows/privacy_overlay.cpp
 * @brief Local black overlay excluded from Windows display capture.
 */

#define OEMRESOURCE

#include "privacy_overlay.h"

#include "src/logging.h"

#include <condition_variable>
#include <array>
#include <dwmapi.h>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <Windows.h>

namespace platf::privacy_overlay {
  /**
   * @brief Read one Windows cursor bitmap without substituting another shape.
   * @param system_id Windows system cursor identifier.
   * @return Exact cursor shape when Windows provides a supported bitmap.
   */
  static std::optional<cursor_shape_t> capture_system_cursor_exact(DWORD system_id) {
    auto cursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(system_id));
    ICONINFO icon {};
    if (!cursor || !GetIconInfo(cursor, &icon)) {
      return std::nullopt;
    }

    auto dc = GetDC(nullptr);
    auto captured = [&]() -> std::optional<cursor_shape_t> {
      if (!dc || !icon.hbmMask) {
        return std::nullopt;
      }

      BITMAP bitmap {};
      const auto color = icon.hbmColor;
      if (GetObjectW(color ? color : icon.hbmMask, sizeof(bitmap), &bitmap) != sizeof(bitmap) ||
          bitmap.bmWidth <= 0 || bitmap.bmWidth > 256 || bitmap.bmHeight <= 0 || bitmap.bmHeight > 512) {
        return std::nullopt;
      }

      cursor_shape_t shape;
      shape.system_id = system_id;
      shape.info.Width = bitmap.bmWidth;
      shape.info.HotSpot = {static_cast<LONG>(icon.xHotspot), static_cast<LONG>(icon.yHotspot)};

      if (!color) {
        if (bitmap.bmHeight % 2 != 0) {
          return std::nullopt;
        }
        shape.info.Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
        shape.info.Height = bitmap.bmHeight;
        shape.info.Pitch = ((bitmap.bmWidth + 31) / 32) * 4;
        shape.pixels.resize(shape.info.Pitch * shape.info.Height);
        struct {
          BITMAPINFOHEADER header;
          RGBQUAD colors[2];
        } info {};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = bitmap.bmWidth;
        info.header.biHeight = -bitmap.bmHeight;
        info.header.biPlanes = 1;
        info.header.biBitCount = 1;
        info.header.biCompression = BI_RGB;
        if (GetDIBits(dc, icon.hbmMask, 0, bitmap.bmHeight, shape.pixels.data(), reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS) != bitmap.bmHeight) {
          return std::nullopt;
        }
      } else {
        shape.info.Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR;
        shape.info.Height = bitmap.bmHeight;
        shape.info.Pitch = bitmap.bmWidth * 4;
        shape.pixels.resize(shape.info.Pitch * shape.info.Height);
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = bitmap.bmWidth;
        info.bmiHeader.biHeight = -bitmap.bmHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        if (GetDIBits(dc, color, 0, bitmap.bmHeight, shape.pixels.data(), &info, DIB_RGB_COLORS) != bitmap.bmHeight) {
          return std::nullopt;
        }

        // Older color cursors store opacity only in the separate AND mask.
        bool has_alpha = false;
        for (std::size_t pixel = 3; pixel < shape.pixels.size(); pixel += 4) {
          has_alpha |= shape.pixels[pixel] != 0;
        }
        if (!has_alpha) {
          BITMAP mask_bitmap {};
          if (GetObjectW(icon.hbmMask, sizeof(mask_bitmap), &mask_bitmap) != sizeof(mask_bitmap) ||
              mask_bitmap.bmWidth != bitmap.bmWidth || mask_bitmap.bmHeight != bitmap.bmHeight) {
            return std::nullopt;
          }
          const auto mask_pitch = ((bitmap.bmWidth + 31) / 32) * 4;
          std::vector<std::uint8_t> mask(mask_pitch * bitmap.bmHeight);
          struct {
            BITMAPINFOHEADER header;
            RGBQUAD colors[2];
          } mask_info {};
          mask_info.header.biSize = sizeof(BITMAPINFOHEADER);
          mask_info.header.biWidth = bitmap.bmWidth;
          mask_info.header.biHeight = -bitmap.bmHeight;
          mask_info.header.biPlanes = 1;
          mask_info.header.biBitCount = 1;
          mask_info.header.biCompression = BI_RGB;
          if (GetDIBits(dc, icon.hbmMask, 0, bitmap.bmHeight, mask.data(), reinterpret_cast<BITMAPINFO *>(&mask_info), DIB_RGB_COLORS) != bitmap.bmHeight) {
            return std::nullopt;
          }
          for (int y = 0; y < bitmap.bmHeight; ++y) {
            for (int x = 0; x < bitmap.bmWidth; ++x) {
              if (!(mask[y * mask_pitch + x / 8] & (0x80 >> (x % 8)))) {
                shape.pixels[y * shape.info.Pitch + x * 4 + 3] = 0xff;
              }
            }
          }
        }
        bool visible_pixel = false;
        for (std::size_t pixel = 3; pixel < shape.pixels.size(); pixel += 4) {
          visible_pixel |= shape.pixels[pixel] != 0;
        }
        if (!visible_pixel) {
          return std::nullopt;
        }
      }
      return shape;
    }();

    if (dc) {
      ReleaseDC(nullptr, dc);
    }
    DeleteObject(icon.hbmMask);
    if (icon.hbmColor) {
      DeleteObject(icon.hbmColor);
    }
    return captured;
  }

  std::optional<cursor_shape_t> capture_system_cursor(DWORD system_id) {
    if (auto shape = capture_system_cursor_exact(system_id)) {
      return shape;
    }
    if (system_id == OCR_NORMAL) {
      return std::nullopt;
    }
    auto arrow = capture_system_cursor_exact(OCR_NORMAL);
    if (arrow) {
      arrow->system_id = system_id;
      arrow->fallback_to_arrow = true;
    }
    return arrow;
  }

  namespace {
    constexpr wchar_t window_class[] = L"SunshinePrivacyOverlay";  ///< Private window class for local covers.
    constexpr UINT refresh_message = WM_APP + 1;  ///< Message used to rebuild windows after display changes.
    constexpr UINT reassert_message = WM_APP + 2;  ///< Move covers above newly shown local popups.
    constexpr DWORD exclude_from_capture = 0x00000011;  ///< WDA_EXCLUDEFROMCAPTURE.

    constexpr std::array<DWORD, 13> cursor_ids {
      OCR_NORMAL, OCR_IBEAM, OCR_WAIT, OCR_CROSS, OCR_UP, OCR_SIZENWSE, OCR_SIZENESW, OCR_SIZEWE,
      OCR_SIZENS, OCR_SIZEALL, OCR_NO, OCR_HAND, OCR_APPSTARTING
    };

    /** @brief Make a one-pixel black cursor; it disappears against the black local cover. */
    HCURSOR create_black_cursor() {
      std::array<BYTE, 128> and_mask;
      and_mask.fill(0xff);
      and_mask[0] = 0x7f;
      std::array<BYTE, 128> xor_mask {};
      return CreateCursor(GetModuleHandleW(nullptr), 0, 0, 32, 32, and_mask.data(), xor_mask.data());
    }

    bool blacken_cursors() {
      for (const auto id : cursor_ids) {
        auto cursor = create_black_cursor();
        if (!cursor || !SetSystemCursor(cursor, id)) {
          if (cursor) {
            DestroyCursor(cursor);
          }
          BOOST_LOG(error) << "Privacy overlay: unable to replace system cursor " << id << ": " << GetLastError();
          return false;
        }
      }
      return true;
    }

    void CALLBACK on_window_event(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
      PostThreadMessageW(GetCurrentThreadId(), reassert_message, 0, 0);
    }

    /** @brief Original cursor shape and the corresponding blackened system handle. */
    struct saved_cursor_t {
      HCURSOR handle {};  ///< System cursor handle after replacement.
      cursor_shape_t shape;  ///< Shape captured before replacement.
    };

    /** @brief Synchronizes synchronous startup with the window message thread. */
    struct state_t {
      std::mutex mutex;
      std::condition_variable ready;
      std::thread worker;
      DWORD worker_id {};
      bool startup_complete {};
      bool startup_success {};
      std::uint64_t generation {};
      std::array<saved_cursor_t, cursor_ids.size()> cursors;
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

    bool reassert_windows(const std::vector<HWND> &windows) {
      for (auto window : windows) {
        if (!SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
          BOOST_LOG(error) << "Privacy overlay: unable to raise cover above popup: " << GetLastError();
          return false;
        }
      }
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
      const bool covers_ready = registered && SUCCEEDED(DwmIsCompositionEnabled(&composition_enabled)) && composition_enabled &&
                                refresh_windows(windows);
      auto show_hook = covers_ready ? SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, on_window_event,
                                                      0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS) : nullptr;
      auto menu_hook = show_hook ? SetWinEventHook(EVENT_SYSTEM_MENUPOPUPSTART, EVENT_SYSTEM_MENUPOPUPSTART, nullptr,
                                                  on_window_event, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS) : nullptr;
      std::array<saved_cursor_t, cursor_ids.size()> cursors;
      bool shapes_ready = covers_ready && show_hook && menu_hook;
      if (shapes_ready) {
        for (std::size_t i = 0; i < cursor_ids.size(); ++i) {
          auto shape = capture_system_cursor(cursor_ids[i]);
          if (!shape) {
            BOOST_LOG(error) << "Privacy overlay: unable to capture system cursor " << cursor_ids[i];
            shapes_ready = false;
            break;
          }
          if (shape->fallback_to_arrow) {
            BOOST_LOG(warning) << "Privacy overlay: using arrow shape for unreadable system cursor " << cursor_ids[i];
          }
          cursors[i].shape = std::move(*shape);
        }
      }
      bool success = shapes_ready && blacken_cursors();
      if (success) {
        for (std::size_t i = 0; i < cursor_ids.size(); ++i) {
          cursors[i].handle = LoadCursorW(nullptr, MAKEINTRESOURCEW(cursor_ids[i]));
          if (!cursors[i].handle) {
            BOOST_LOG(error) << "Privacy overlay: unable to identify blackened system cursor " << cursor_ids[i];
            success = false;
            break;
          }
        }
      }
      {
        std::lock_guard lock(state.mutex);
        state.worker_id = GetCurrentThreadId();
        if (success) {
          ++state.generation;
          for (auto &saved : cursors) {
            saved.shape.generation = state.generation;
          }
          state.cursors = std::move(cursors);
        }
        state.startup_success = success;
        state.startup_complete = true;
      }
      state.ready.notify_one();
      if (!success) {
        if (menu_hook) {
          UnhookWinEvent(menu_hook);
        }
        if (show_hook) {
          UnhookWinEvent(show_hook);
        }
        restore_cursors();
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
        } else if (message.message == reassert_message) {
          if (!reassert_windows(windows) && !failure_reported && state.coverage_lost) {
            failure_reported = true;
            state.coverage_lost();
          }
        } else {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
      }
      UnhookWinEvent(menu_hook);
      UnhookWinEvent(show_hook);
      restore_cursors();
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

  bool restore_cursors() {
    const bool restored = SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
    if (!restored) {
      BOOST_LOG(error) << "Privacy overlay: unable to restore system cursors: " << GetLastError();
    }
    return restored;
  }

  std::optional<cursor_shape_t> stream_cursor_shape() {
    CURSORINFO current {sizeof(current)};
    if (!GetCursorInfo(&current)) {
      return std::nullopt;
    }
    std::lock_guard lock(state.mutex);
    if (!state.startup_success) {
      return std::nullopt;
    }
    for (const auto &saved : state.cursors) {
      if (current.hCursor == saved.handle) {
        auto shape = saved.shape;
        shape.screen_position = current.ptScreenPos;
        shape.visible = (current.flags & CURSOR_SHOWING) != 0;
        return shape;
      }
    }
    return std::nullopt;
  }

  std::optional<POINT> cursor_top_left_on_output(const cursor_shape_t &shape, LONG output_left, LONG output_top, LONG width, LONG height) {
    const auto x = static_cast<std::int64_t>(shape.screen_position.x) - output_left;
    const auto y = static_cast<std::int64_t>(shape.screen_position.y) - output_top;
    if (!shape.visible || x < 0 || y < 0 || x >= width || y >= height) {
      return std::nullopt;
    }
    return POINT {static_cast<LONG>(x - shape.info.HotSpot.x), static_cast<LONG>(y - shape.info.HotSpot.y)};
  }
}  // namespace platf::privacy_overlay
