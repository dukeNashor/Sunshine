/**
 * @file src/platform/windows/privacy_overlay.h
 * @brief Local black overlay excluded from Windows display capture.
 */
#pragma once

#include <functional>

namespace platf::privacy_overlay {
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
