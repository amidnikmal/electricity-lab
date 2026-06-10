#pragma once

#include <algorithm>
#include <cmath>

// HiDPI helpers: pure logic, unit-testable without a window system.
// The windowing system reports a monitor "content scale" (1.0 = 96 dpi,
// 1.75 = 175% Windows scaling, ...). Fonts, style paddings and the initial
// window size are multiplied by this factor so the UI stays physically
// readable on dense displays.
namespace current_lab::app {

// Sanitizes a reported content scale: NaN / zero / negative / absurd values
// (broken driver, headless stub) collapse to 1.0; real values are clamped to
// a range the layout can actually survive.
inline float clampUiScale(float reported) {
    if (!(reported >= 0.5f && reported <= 16.0f)) return 1.0f; // catches NaN too
    return std::min(reported, 4.0f);
}

// Font rasterization wants whole pixels: scale, round, keep readable minimum.
inline float scaledFontSize(float basePx, float contentScale) {
    return std::max(8.0f, std::round(basePx * clampUiScale(contentScale)));
}

// Initial window size in physical pixels for a desired logical size.
inline int scaledWindowDimension(int logicalPx, float contentScale) {
    return static_cast<int>(std::lround(logicalPx * clampUiScale(contentScale)));
}

} // namespace current_lab::app
