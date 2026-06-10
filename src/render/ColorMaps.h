#pragma once

#include "render/RenderPrimitives.h"
#include <algorithm>

// Shared colour mapping for solver values. Pure math, no ImGui, testable.
namespace current_lab::render {

inline uint32_t blendColor(uint32_t a, uint32_t b, double t) {
    auto chan = [](uint32_t c, int shift) { return static_cast<int>((c >> shift) & 0xFF); };
    double u = std::clamp(t, 0.0, 1.0);
    int r = static_cast<int>(chan(a, 0) + (chan(b, 0) - chan(a, 0)) * u);
    int g = static_cast<int>(chan(a, 8) + (chan(b, 8) - chan(a, 8)) * u);
    int bb = static_cast<int>(chan(a, 16) + (chan(b, 16) - chan(a, 16)) * u);
    int al = static_cast<int>(chan(a, 24) + (chan(b, 24) - chan(a, 24)) * u);
    return packColor(r, g, bb, al);
}

inline uint32_t potentialColor(double v, double vMin, double vMax) {
    double range = vMax - vMin;
    if (range < 1e-12) return packColor(93, 128, 196, 220);
    double t = std::clamp((v - vMin) / range, 0.0, 1.0);

    const uint32_t stop0 = packColor(49, 78, 130, 220);
    const uint32_t stop1 = packColor(72, 136, 170, 220);
    const uint32_t stop2 = packColor(213, 170, 82, 220);
    const uint32_t stop3 = packColor(211, 92, 68, 220);

    if (t < 0.35) return blendColor(stop0, stop1, t / 0.35);
    if (t < 0.75) return blendColor(stop1, stop2, (t - 0.35) / 0.40);
    return blendColor(stop2, stop3, (t - 0.75) / 0.25);
}

inline uint32_t currentColor(double absI, double maxI) {
    if (maxI < 1e-12) return packColor(110, 170, 224, 220);
    double t = std::min(absI / maxI, 1.0);
    return blendColor(packColor(102, 174, 216, 220), packColor(245, 118, 66, 220), t);
}

} // namespace current_lab::render
