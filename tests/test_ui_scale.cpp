#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "app/UiScale.h"

using namespace current_lab::app;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

} // namespace

// --- clampUiScale: sanitizing the reported monitor content scale --------------

TEST(ClampUiScale, IdentityForCommonScales) {
    EXPECT_FLOAT_EQ(clampUiScale(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(1.25f), 1.25f);
    EXPECT_FLOAT_EQ(clampUiScale(1.5f), 1.5f);
    EXPECT_FLOAT_EQ(clampUiScale(1.75f), 1.75f);
    EXPECT_FLOAT_EQ(clampUiScale(2.0f), 2.0f);
    EXPECT_FLOAT_EQ(clampUiScale(3.0f), 3.0f);
}

TEST(ClampUiScale, HugeButSaneScalesClampToFour) {
    // Values the predicate accepts as "real" (<= 16) are still capped at 4x,
    // the largest factor the layout survives.
    EXPECT_FLOAT_EQ(clampUiScale(4.5f), 4.0f);
    EXPECT_FLOAT_EQ(clampUiScale(5.0f), 4.0f);
    EXPECT_FLOAT_EQ(clampUiScale(8.0f), 4.0f);
    EXPECT_FLOAT_EQ(clampUiScale(12.0f), 4.0f);
    EXPECT_FLOAT_EQ(clampUiScale(15.99f), 4.0f);
    EXPECT_FLOAT_EQ(clampUiScale(16.0f), 4.0f);
}

TEST(ClampUiScale, GarbageCollapsesToOne) {
    EXPECT_FLOAT_EQ(clampUiScale(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(-0.0f), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(-1.0f), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(-2.0f), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(kNaN), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(kInf), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(-kInf), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(0.49f), 1.0f);  // just below the floor
    EXPECT_FLOAT_EQ(clampUiScale(16.01f), 1.0f); // just above the ceiling
    EXPECT_FLOAT_EQ(clampUiScale(std::numeric_limits<float>::denorm_min()), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(std::numeric_limits<float>::lowest()), 1.0f);
    EXPECT_FLOAT_EQ(clampUiScale(std::numeric_limits<float>::max()), 1.0f);
}

TEST(ClampUiScale, FloorBoundaryHalfIsInclusive) {
    // The predicate is `reported >= 0.5f`: exactly 0.5 is accepted as-is...
    EXPECT_FLOAT_EQ(clampUiScale(0.5f), 0.5f);
    // ...but one ulp below it is garbage and collapses to 1.0.
    EXPECT_FLOAT_EQ(clampUiScale(std::nextafter(0.5f, 0.0f)), 1.0f);
}

TEST(ClampUiScale, CeilingBoundarySixteenIsInclusiveYetStillClampedToFour) {
    // The predicate is `reported <= 16.0f`: exactly 16 counts as "real"
    // and is then clamped to 4.0; one ulp above 16 collapses to 1.0.
    // Pinning this discontinuity: 16.0 -> 4.0, 16.0+ulp -> 1.0.
    EXPECT_FLOAT_EQ(clampUiScale(16.0f), 4.0f);
    EXPECT_FLOAT_EQ(clampUiScale(std::nextafter(16.0f, kInf)), 1.0f);
}

TEST(ClampUiScale, FourPointZeroPassesThroughUnclamped) {
    EXPECT_FLOAT_EQ(clampUiScale(4.0f), 4.0f);
    // One ulp above 4 already hits the std::min cap.
    EXPECT_FLOAT_EQ(clampUiScale(std::nextafter(4.0f, kInf)), 4.0f);
}

// --- scaledFontSize: whole pixels, readable minimum ----------------------------

TEST(ScaledFontSize, SixteenPxBaseAtCommonScales) {
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 1.0f), 16.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 1.25f), 20.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 1.5f), 24.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 1.75f), 28.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 2.0f), 32.0f);
}

TEST(ScaledFontSize, RoundsToWholePixels) {
    EXPECT_FLOAT_EQ(scaledFontSize(13.0f, 1.15f), 15.0f); // 14.95 -> 15
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 1.1f), 18.0f);  // 17.6 -> 18
    EXPECT_FLOAT_EQ(scaledFontSize(13.0f, 1.5f), 20.0f);  // 19.5, half away from zero
    EXPECT_FLOAT_EQ(scaledFontSize(10.0f, 1.25f), 13.0f); // 12.5, half away from zero
    EXPECT_FLOAT_EQ(scaledFontSize(15.0f, 0.75f), 11.0f); // 11.25 -> 11
}

TEST(ScaledFontSize, EnforcesEightPixelMinimum) {
    EXPECT_FLOAT_EQ(scaledFontSize(2.0f, kNaN), 8.0f);  // garbage scale, tiny base
    EXPECT_FLOAT_EQ(scaledFontSize(1.0f, 0.5f), 8.0f);  // valid sub-1.0 scale
    EXPECT_FLOAT_EQ(scaledFontSize(4.0f, 1.0f), 8.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(5.0f, 1.5f), 8.0f);  // 7.5 rounds to 8, at the floor
    EXPECT_FLOAT_EQ(scaledFontSize(0.0f, 2.0f), 8.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(8.0f, 1.0f), 8.0f);  // exactly at the floor
    EXPECT_FLOAT_EQ(scaledFontSize(9.0f, 1.0f), 9.0f);  // just above it
}

TEST(ScaledFontSize, GarbageScaleFallsBackToBaseSize) {
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, kNaN), 16.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 0.0f), 16.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, -2.0f), 16.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, kInf), 16.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(13.0f, 0.49f), 13.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(24.0f, 16.01f), 24.0f);
}

TEST(ScaledFontSize, NegativeBaseCollapsesToMinimum) {
    // There is no guard on basePx; the 8px floor swallows nonsense bases.
    EXPECT_FLOAT_EQ(scaledFontSize(-16.0f, 1.0f), 8.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(-1.0f, 4.0f), 8.0f);
}

TEST(ScaledFontSize, NanBaseCollapsesToMinimum) {
    // round(NaN * 1.0) is NaN; std::max(8.0f, NaN) evaluates (8 < NaN) == false
    // and returns its first argument, so a NaN base degrades to the 8px floor.
    EXPECT_FLOAT_EQ(scaledFontSize(kNaN, 1.0f), 8.0f);
}

// --- scaledWindowDimension: physical pixels for a logical size -----------------

TEST(ScaledWindowDimension, CommonScalesForDefaultWindow) {
    EXPECT_EQ(scaledWindowDimension(1280, 1.0f), 1280);
    EXPECT_EQ(scaledWindowDimension(800, 1.0f), 800);
    EXPECT_EQ(scaledWindowDimension(1280, 1.75f), 2240);
    EXPECT_EQ(scaledWindowDimension(800, 1.75f), 1400);
    EXPECT_EQ(scaledWindowDimension(1280, 2.0f), 2560);
    EXPECT_EQ(scaledWindowDimension(800, 2.0f), 1600);
}

TEST(ScaledWindowDimension, RoundsToNearestPhysicalPixel) {
    EXPECT_EQ(scaledWindowDimension(1281, 1.75f), 2242); // 2241.75 -> 2242
    EXPECT_EQ(scaledWindowDimension(801, 1.75f), 1402);  // 1401.75 -> 1402
    EXPECT_EQ(scaledWindowDimension(1001, 1.25f), 1251); // 1251.25 -> 1251
    EXPECT_EQ(scaledWindowDimension(3, 1.5f), 5);        // 4.5, half away from zero
}

TEST(ScaledWindowDimension, GarbageScaleKeepsLogicalSize) {
    EXPECT_EQ(scaledWindowDimension(1280, kNaN), 1280);
    EXPECT_EQ(scaledWindowDimension(1280, 0.0f), 1280);
    EXPECT_EQ(scaledWindowDimension(800, -2.0f), 800);
    EXPECT_EQ(scaledWindowDimension(1280, kInf), 1280);
    EXPECT_EQ(scaledWindowDimension(800, 16.01f), 800);
    EXPECT_EQ(scaledWindowDimension(1280, 0.49f), 1280);
}

TEST(ScaledWindowDimension, HugeScaleCapsAtFourTimesLogical) {
    EXPECT_EQ(scaledWindowDimension(1280, 9.5f), 5120);
    EXPECT_EQ(scaledWindowDimension(1280, 16.0f), 5120); // inclusive ceiling, then 4x cap
    EXPECT_EQ(scaledWindowDimension(800, 16.0f), 3200);
}

TEST(ScaledWindowDimension, ZeroLogicalStaysZero) {
    EXPECT_EQ(scaledWindowDimension(0, 1.0f), 0);
    EXPECT_EQ(scaledWindowDimension(0, 2.0f), 0);
    EXPECT_EQ(scaledWindowDimension(0, kNaN), 0);
}

TEST(ScaledWindowDimension, NegativeLogicalScalesWithoutFloor) {
    // Unlike fonts there is no minimum: negative logical sizes pass through scaled.
    EXPECT_EQ(scaledWindowDimension(-100, 2.0f), -200);
    EXPECT_EQ(scaledWindowDimension(-100, kNaN), -100);
}

// --- Additional edge cases ------------------------------------------------------

TEST(ClampUiScale, IsIdempotent) {
    // Every output of clampUiScale is a fixed point: sanitizing twice changes nothing.
    const float samples[] = {kNaN, -kInf, -1.0f, 0.0f, 0.49f, 0.5f, 0.75f,
                             1.0f, 1.75f, 4.0f, 4.5f, 16.0f, 16.01f, kInf};
    for (float s : samples) {
        EXPECT_FLOAT_EQ(clampUiScale(clampUiScale(s)), clampUiScale(s)) << "sample: " << s;
    }
}

TEST(ScaledFontSize, LargeValidScaleIsCappedAtFourTimes) {
    // Scales in (4, 16] are "real" but the font still grows at most 4x.
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 8.0f), 64.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(16.0f, 16.0f), 64.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(13.0f, 12.0f), 52.0f);
}

TEST(ScaledFontSize, FractionalBaseRoundsToWholePixels) {
    EXPECT_FLOAT_EQ(scaledFontSize(13.5f, 1.0f), 14.0f); // half away from zero
    EXPECT_FLOAT_EQ(scaledFontSize(12.4f, 1.0f), 12.0f);
    EXPECT_FLOAT_EQ(scaledFontSize(12.6f, 1.0f), 13.0f);
}

TEST(ScaledFontSize, InfiniteBaseHasNoUpperGuard) {
    // Only the scale is sanitized; an infinite base passes straight through.
    EXPECT_EQ(scaledFontSize(kInf, 1.0f), kInf);
}

TEST(ScaledWindowDimension, HalfScaleShrinksAndRoundsHalfAwayFromZero) {
    EXPECT_EQ(scaledWindowDimension(1280, 0.5f), 640);
    EXPECT_EQ(scaledWindowDimension(801, 0.5f), 401); // 400.5 -> 401
    EXPECT_EQ(scaledWindowDimension(1, 0.5f), 1);     // 0.5 -> 1, window never vanishes
}
