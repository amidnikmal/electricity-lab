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

// Выборка из равномерного LUT с линейной интерполяцией между опорными точками.
// lut — массив из n цветов для t = 0/(n-1), 1/(n-1), ..., 1.
inline uint32_t lutSample(const uint32_t* lut, int n, double t) {
    t = std::clamp(t, 0.0, 1.0);
    double idx = t * (n - 1);
    int i = static_cast<int>(idx);
    if (i >= n - 1) return lut[n - 1];
    double frac = idx - i;
    return blendColor(lut[i], lut[i + 1], frac);
}

// Viridis LUT: перцептивно-равномерная палитра (BIDS/matplotlib, van der Walt & Smith 2015).
// 17 опорных точек вдоль [0, 1]; светлота L* монотонно растёт (~15 → ~91),
// без ложных границ (false boundaries). Colourblind-safe, grayscale-конвертируема.
// В отличие от rainbow/jet, viridis не создаёт артефактов на переходах hue
// (Rogowitz & Treinish, «Why Should Engineers and Scientists Be Worried About Color?», 1995).
// Источник: https://bids.github.io/colormap/
inline constexpr int kViridisLutSize = 17;
inline constexpr uint32_t kViridisLut[kViridisLutSize] = {
    packColor(68,  1,   84,  255), // t=0.0000 deep violet
    packColor(72,  11,  96,  255), // t=0.0625
    packColor(71,  25,  106, 255), // t=0.1250
    packColor(68,  39,  115, 255), // t=0.1875
    packColor(63,  54,  123, 255), // t=0.2500
    packColor(55,  66,  130, 255), // t=0.3125
    packColor(47,  78,  135, 255), // t=0.3750
    packColor(40,  89,  138, 255), // t=0.4375
    packColor(36,  100, 138, 255), // t=0.5000
    packColor(37,  110, 134, 255), // t=0.5625
    packColor(49,  120, 128, 255), // t=0.6250
    packColor(70,  130, 118, 255), // t=0.6875
    packColor(94,  142, 104, 255), // t=0.7500
    packColor(122, 153, 88,  255), // t=0.8125
    packColor(154, 163, 70,  255), // t=0.8750
    packColor(190, 175, 49,  255), // t=0.9375
    packColor(253, 231, 37,  255), // t=1.0000 bright yellow
};

// Magma LUT: перцептивно-равномерная sequential-палитра (BIDS/matplotlib).
// 8 опорных точек: чёрно-фиолетовый → красный → оранжевый → светло-жёлтый.
// Монотонная светлота, colourblind-safe. Используется для currentColor.
inline constexpr int kMagmaLutSize = 8;
inline constexpr uint32_t kMagmaLut[kMagmaLutSize] = {
    packColor(0,   0,   4,   255), // t=0.0000 black
    packColor(28,  14,  66,  255), // t=0.1429
    packColor(78,  28,  96,  255), // t=0.2857
    packColor(140, 46,  98,  255), // t=0.4286
    packColor(198, 72,  78,  255), // t=0.5714
    packColor(236, 112, 53,  255), // t=0.7143
    packColor(251, 168, 43,  255), // t=0.8571
    packColor(252, 253, 191, 255), // t=1.0000 pale yellow
};

inline uint32_t potentialColor(double v, double vMin, double vMax) {
    double range = vMax - vMin;
    if (range < 1e-12) return kViridisLut[0];
    double t = std::clamp((v - vMin) / range, 0.0, 1.0);
    return lutSample(kViridisLut, kViridisLutSize, t);
}

inline uint32_t currentColor(double absI, double maxI) {
    if (maxI < 1e-12) return kMagmaLut[0];
    double t = std::min(absI / maxI, 1.0);
    return lutSample(kMagmaLut, kMagmaLutSize, t);
}

} // namespace current_lab::render
