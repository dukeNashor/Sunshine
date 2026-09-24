/**
 * @file src/platform/windows/privacy_overlay.h
 * @brief Local black overlay excluded from Windows display capture.
 */
#pragma once

#include <cstdint>
#include <dxgi1_2.h>
#include <functional>
#include <optional>
#include <vector>

namespace platf::privacy_overlay {
  /** @brief Original system cursor to composite into a privacy desktop stream. */
  struct cursor_shape_t {
    DWORD system_id {};  ///< Windows system cursor identifier.
    std::uint64_t generation {};  ///< Overlay start generation for GPU texture caching.
    DXGI_OUTDUPL_POINTER_SHAPE_INFO info {};  ///< DXGI-compatible color shape metadata.
    std::vector<std::uint8_t> pixels;  ///< Top-down DXGI color pixels or monochrome AND/XOR masks.
    POINT screen_position {};  ///< Current pointer hotspot in virtual desktop coordinates.
    bool visible {};  ///< Whether Windows currently displays this pointer.
    bool fallback_to_arrow {};  ///< Whether the original cursor could not be read and uses the arrow shape.
  };

  /**
   * @brief Read a system cursor before the local cursor scheme is blackened.
   * @param system_id Windows system cursor identifier.
   * @return Original shape, an arrow fallback, or no value if neither can be read.
   */
  std::optional<cursor_shape_t> capture_system_cursor(DWORD system_id);

  /**
   * @brief Look up the original shape of the current blackened standard cursor.
   * @return Original shape while a privacy overlay is active, otherwise no value.
   */
  std::optional<cursor_shape_t> stream_cursor_shape();

  /**
   * @brief Convert a screen-space pointer hotspot to a captured output's top-left shape position.
   * @param shape Original cursor shape and current hotspot position.
   * @param output_left Captured output left edge in virtual desktop coordinates.
   * @param output_top Captured output top edge in virtual desktop coordinates.
   * @param width Captured output width.
   * @param height Captured output height.
   * @return Cursor shape top-left relative to the output, or no value outside that output.
   */
  std::optional<POINT> cursor_top_left_on_output(const cursor_shape_t &shape, LONG output_left, LONG output_top, LONG width, LONG height);

  /**
   * @brief Cover all active monitors before video capture starts.
   * @param coverage_lost Called if a later display change cannot be covered.
   * @return True if every overlay is visible and excluded from capture.
   */
  bool start(std::function<void()> coverage_lost = {});

  /**
   * @brief Remove all privacy overlays after the last streaming session ends.
   */
  void stop();

  /** @brief Reload the user's saved Windows cursor scheme after an interrupted session. */
  bool restore_cursors();
}  // namespace platf::privacy_overlay
