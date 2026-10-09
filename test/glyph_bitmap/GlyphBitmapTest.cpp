#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <utility>
#include <vector>

#include "lib/GfxRenderer/GlyphBitmap.h"
#include "lib/GfxRenderer/VerticalText.h"

namespace {
constexpr int PANEL_WIDTH = 40;
constexpr int PANEL_HEIGHT = 32;
constexpr int STRIDE = PANEL_WIDTH / 8;

using glyphBitmap::Plane;

// Logical placements used by the renderer: unrotated text, and side-button
// labels rotated 90 degrees clockwise (glyph x runs up, glyph y runs right).
constexpr std::array<std::array<int, 4>, 2> AXES = {{{1, 0, 0, 1}, {0, -1, 1, 0}}};

std::pair<int, int> physical(int orientation, int x, int y) {
  switch (orientation) {
    case 0:
      return {y, PANEL_HEIGHT - 1 - x};
    case 1:
      return {PANEL_WIDTH - 1 - x, PANEL_HEIGHT - 1 - y};
    case 2:
      return {PANEL_WIDTH - 1 - y, x};
    default:
      return {x, y};
  }
}

// Logical rectangle the renderer clips to; the glyph-local clip is derived from it.
struct Rect {
  int left;
  int top;
  int right;
  int bottom;
};

void compare(int orientation, int rotation, bool twoBit, Plane plane, bool state, int width, int height, int x, int y,
             int originY, int rows, Rect clip) {
  SCOPED_TRACE(::testing::Message() << orientation << ',' << rotation << ',' << twoBit << ',' << static_cast<int>(plane)
                                    << ',' << state << " size=" << width << 'x' << height << " at=" << x << ',' << y
                                    << " band=" << originY << ',' << rows);
  const auto [dxX, dxY, dyX, dyY] = AXES[rotation];
  const glyphBitmap::Frame logical{x, y, dxX, dxY, dyX, dyY};
  std::vector<uint8_t> bitmap((width * height * (twoBit ? 2 : 1) + 7) / 8);
  for (size_t i = 0; i < bitmap.size(); ++i) bitmap[i] = static_cast<uint8_t>(i * 73 + 0x1b);
  // Nonuniform background and guards verify transparent pixels and all off-band bytes.
  std::vector<uint8_t> expected(rows * STRIDE + 32);
  for (size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<uint8_t>(i * 53 + 0xa5);
  auto actual = expected;
  for (int gy = 0; gy < height; ++gy) {
    for (int gx = 0; gx < width; ++gx) {
      const int lx = x + gx * dxX + gy * dyX;
      const int ly = y + gx * dxY + gy * dyY;
      if (lx < clip.left || ly < clip.top || lx >= clip.right || ly >= clip.bottom) continue;
      const auto [px, py] = physical(orientation, lx, ly);
      if (px < 0 || px >= PANEL_WIDTH || py < originY || py >= originY + rows) continue;
      const int source = gy * width + gx;
      bool draw = false;
      bool black = state;
      if (twoBit) {
        const int value = 3 - ((bitmap[source / 4] >> ((3 - source % 4) * 2)) & 3);
        if (plane == Plane::BW) draw = value < 3;
        if (plane == Plane::GrayLSB) draw = value == 1;
        if (plane == Plane::GrayMSB) draw = value == 1 || value == 2;
        if (plane != Plane::BW) black = false;
      } else {
        draw = (bitmap[source / 8] >> (7 - source % 8)) & 1;
      }
      if (!draw) continue;
      auto& byte = expected[16 + (py - originY) * STRIDE + px / 8];
      if (black)
        byte &= static_cast<uint8_t>(~(0x80 >> (px % 8)));
      else
        byte |= 0x80 >> (px % 8);
    }
  }

  glyphBitmap::Clip local{0, 0, width, height};
  glyphBitmap::clipToRect(logical, clip.left, clip.top, clip.right, clip.bottom, local);

  const auto [px, py] = physical(orientation, x, y);
  const auto [xx, xy] = physical(orientation, x + dxX, y + dxY);
  const auto [yx, yy] = physical(orientation, x + dyX, y + dyY);
  const glyphBitmap::Target target{
      actual.data() + 16, PANEL_WIDTH, STRIDE, originY, rows, {px, py, xx - px, xy - py, yx - px, yy - py}};
  glyphBitmap::draw(bitmap.data(), width, height, twoBit, plane, state, target, local);
  EXPECT_EQ(expected, actual);
}
}  // namespace

TEST(GlyphBitmap, MatchesPerPixelReferenceAcrossOrientationsRotationsPlanesAndClipping) {
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int rotation = 0; rotation < 2; ++rotation) {
      for (bool twoBit : {false, true}) {
        for (Plane plane : {Plane::BW, Plane::GrayLSB, Plane::GrayMSB}) {
          for (bool state : {false, true}) {
            for (int width : {1, 3, 7, 16, 31}) {
              for (const auto [x, y] : {std::pair{-5, -3}, std::pair{0, 0}, std::pair{9, 11}, std::pair{29, 31}}) {
                for (const auto [origin, rows] :
                     {std::pair{0, 32}, std::pair{0, 7}, std::pair{7, 11}, std::pair{28, 4}}) {
                  compare(orientation, rotation, twoBit, plane, state, width, 13, x, y, origin, rows,
                          {-20, -20, 60, 60});
                  compare(orientation, rotation, twoBit, plane, state, width, 13, x, y, origin, rows, {2, 3, 18, 20});
                }
              }
            }
          }
        }
      }
    }
  }
}

TEST(GlyphBitmap, EmptyAndFullyClippedGlyphsDoNotWrite) {
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int rotation = 0; rotation < 2; ++rotation) {
      compare(orientation, rotation, true, Plane::BW, true, 0, 0, 0, 0, 0, 32, {0, 0, 40, 32});
      compare(orientation, rotation, true, Plane::BW, true, 11, 9, 100, 100, 0, 32, {0, 0, 40, 32});
      compare(orientation, rotation, true, Plane::BW, true, 11, 9, 0, 0, 0, 32, {12, 10, 15, 20});
    }
  }
}

TEST(GlyphBitmap, VerticalSidewaysPlacementMatchesClockwiseRotationOfCenteredLineBox) {
  for (int cellSize : {20, 21, 42}) {
    for (const auto [ascender, descender] : {std::pair{45, -12}, std::pair{37, -5}, std::pair{18, -3}}) {
      for (const auto [left, top] : {std::pair{23, 35}, std::pair{4, 18}, std::pair{3, 18}, std::pair{-2, 10}}) {
        const auto frame = verticalText::sidewaysGlyphFrame(7, 11, cellSize, ascender, descender, left, top);
        const int lineHeight = ascender - descender;
        const int horizontalTop = (cellSize - lineHeight) / 2 + ascender - top;
        for (int gy = 0; gy < 13; ++gy) {
          for (int gx = 0; gx < 15; ++gx) {
            const int rotatedX = 7 + cellSize - 1 - (horizontalTop + gy);
            const int rotatedY = 11 + left + gx;
            // The device orientation is a separate transform applied after glyph rotation.
            for (int orientation = 0; orientation < 4; ++orientation) {
              EXPECT_EQ(physical(orientation, rotatedX, rotatedY),
                        physical(orientation, frame.x + gx * frame.dxX + gy * frame.dyX,
                                 frame.y + gx * frame.dxY + gy * frame.dyY));
            }
          }
        }
      }
    }
  }
}

TEST(GlyphBitmap, VerticalFallbackLongVowelMarkStaysAtColumnCenter) {
  // Arial Unicode at 20pt/150dpi: 42px em, 45px ascender, -12px descender,
  // horizontal long-vowel bitmap at (3, baseline - 18), measuring 36x4.
  const auto frame = verticalText::sidewaysGlyphFrame(0, 0, 42, 45, -12, 3, 18);
  EXPECT_EQ(frame.x, 21);
  EXPECT_EQ(frame.x + 3 * frame.dyX, 18);
  EXPECT_EQ(frame.y, 3);
  EXPECT_EQ(frame.y + 35 * frame.dxY, 38);
}

TEST(GlyphBitmap, VerticalSmallKanaUsesCenteredRightAlignedPlacement) {
  // Arial Unicode at 20pt/150dpi: 42px em, small tsu 26x20, small ke 27x27.
  EXPECT_TRUE(verticalText::smallKana(0x3063));
  EXPECT_TRUE(verticalText::smallKana(0x30f6));
  EXPECT_FALSE(verticalText::smallKana(0x3064));
  EXPECT_FALSE(verticalText::smallKana(0x30b1));
  const auto tsu = verticalText::smallKanaGlyphFrame(0, 0, 42, 26, 20);
  EXPECT_EQ(tsu.x, 16);
  EXPECT_EQ(tsu.y, 11);
  const auto ke = verticalText::smallKanaGlyphFrame(0, 0, 42, 27, 27);
  EXPECT_EQ(ke.x, 15);
  EXPECT_EQ(ke.y, 7);
}

TEST(GlyphBitmap, VerticalSmallKanaCentersOddAndScaledGlyphsAcrossOrientations) {
  for (const auto [cellSize, width, height] :
       {std::array{25, 16, 12}, std::array{25, 17, 16}, std::array{42, 13, 10}, std::array{42, 14, 14},
        std::array{21, 13, 10}, std::array{21, 14, 14}}) {
    for (const auto [x, y] : {std::pair{0, 0}, std::pair{7, 11}}) {
      SCOPED_TRACE(::testing::Message() << cellSize << ',' << width << ',' << height << " at=" << x << ',' << y);
      const auto frame = verticalText::smallKanaGlyphFrame(x, y, cellSize, width, height);
      EXPECT_EQ(frame.x + width, x + cellSize);
      EXPECT_EQ(frame.dxX, 1);
      EXPECT_EQ(frame.dxY, 0);
      EXPECT_EQ(frame.dyX, 0);
      EXPECT_EQ(frame.dyY, 1);
      for (int orientation = 0; orientation < 4; ++orientation) {
        const auto [cellTopX, cellTopY] = physical(orientation, frame.x, y);
        const auto [cellBottomX, cellBottomY] = physical(orientation, frame.x, y + cellSize - 1);
        const auto [glyphTopX, glyphTopY] = physical(orientation, frame.x, frame.y);
        const auto [glyphBottomX, glyphBottomY] =
            physical(orientation, frame.x + (height - 1) * frame.dyX, frame.y + (height - 1) * frame.dyY);
        // Twice the physical center differs by at most one pixel for odd padding.
        EXPECT_LE(std::abs(glyphTopX + glyphBottomX - cellTopX - cellBottomX), 1);
        EXPECT_LE(std::abs(glyphTopY + glyphBottomY - cellTopY - cellBottomY), 1);
      }
    }
  }
}
