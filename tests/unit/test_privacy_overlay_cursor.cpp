/**
 * @file tests/unit/test_privacy_overlay_cursor.cpp
 * @brief Windows privacy-overlay cursor shape tests.
 */

#ifdef _WIN32
  #define OEMRESOURCE
  #include <Windows.h>
  #include <gtest/gtest.h>

  #include "src/platform/windows/privacy_overlay.h"

namespace {
  /** @brief Every blackened standard cursor must have a usable saved stream shape. */
  TEST(PrivacyOverlayCursor, CapturesEveryStandardShape) {
    constexpr DWORD cursor_ids[] {
      OCR_NORMAL, OCR_IBEAM, OCR_WAIT, OCR_CROSS, OCR_UP, OCR_SIZENWSE, OCR_SIZENESW, OCR_SIZEWE,
      OCR_SIZENS, OCR_SIZEALL, OCR_NO, OCR_HAND, OCR_APPSTARTING
    };
    for (const auto id : cursor_ids) {
      SCOPED_TRACE(id);
      const auto shape = platf::privacy_overlay::capture_system_cursor(id);
      ASSERT_TRUE(shape.has_value());
      EXPECT_EQ(shape->system_id, id);
      EXPECT_GT(shape->info.Width, 0u);
      EXPECT_GT(shape->info.Height, 0u);
      EXPECT_LT(shape->info.HotSpot.x, static_cast<LONG>(shape->info.Width));
      EXPECT_LT(shape->info.HotSpot.y, static_cast<LONG>(shape->info.Height));
      EXPECT_EQ(shape->pixels.size(), shape->info.Pitch * shape->info.Height);
      if (shape->info.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
        bool visible_pixel = false;
        for (std::size_t pixel = 3; pixel < shape->pixels.size(); pixel += 4) {
          visible_pixel |= shape->pixels[pixel] != 0;
        }
        EXPECT_TRUE(visible_pixel);
      }
    }
  }

  /** @brief A process without a privacy overlay has no stream-only cursor. */
  TEST(PrivacyOverlayCursor, NoOverrideOutsidePrivacySession) {
    EXPECT_FALSE(platf::privacy_overlay::stream_cursor_shape().has_value());
  }

  /** @brief A baked-in black pointer can be replaced using its screen-space hotspot. */
  TEST(PrivacyOverlayCursor, PositionsShapeOnCapturedOutput) {
    platf::privacy_overlay::cursor_shape_t shape;
    shape.visible = true;
    shape.screen_position = {100, 200};
    shape.info.HotSpot = {7, 11};

    auto position = platf::privacy_overlay::cursor_top_left_on_output(shape, 50, 100, 200, 200);
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->x, 43);
    EXPECT_EQ(position->y, 89);

    EXPECT_FALSE(platf::privacy_overlay::cursor_top_left_on_output(shape, 101, 100, 200, 200).has_value());
    shape.visible = false;
    EXPECT_FALSE(platf::privacy_overlay::cursor_top_left_on_output(shape, 50, 100, 200, 200).has_value());
  }
}  // namespace
#endif
