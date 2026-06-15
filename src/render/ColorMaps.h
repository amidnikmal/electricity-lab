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
// 17 опорных точек: чёрно-фиолетовый → красный → оранжевый → светло-жёлтый.
// Монотонная светлота, colourblind-safe. Используется для currentColor.
inline constexpr int kMagmaLutSize = 17;
inline constexpr uint32_t kMagmaLut[kMagmaLutSize] = {
    packColor(0,   0,   4,   255), // t=0.0000
    packColor(22,  8,   52,  255), // t=0.0625
    packColor(50,  15,  80,  255), // t=0.1250
    packColor(81,  25,  97,  255), // t=0.1875
    packColor(114, 37,  103, 255), // t=0.2500
    packColor(145, 50,  97,  255), // t=0.3125
    packColor(172, 63,  87,  255), // t=0.3750
    packColor(197, 76,  78,  255), // t=0.4375
    packColor(219, 89,  70,  255), // t=0.5000
    packColor(237, 101, 61,  255), // t=0.5625
    packColor(250, 115, 51,  255), // t=0.6250
    packColor(254, 132, 39,  255), // t=0.6875
    packColor(252, 150, 31,  255), // t=0.7500
    packColor(248, 168, 28,  255), // t=0.8125
    packColor(245, 184, 30,  255), // t=0.8750
    packColor(240, 201, 37,  255), // t=0.9375
    packColor(254, 253, 191, 255), // t=1.0000
};

// Небольшая переиспользуемая абстракция: выбор палитры по имени.
// Добавление новой карты = новый enum-кейс + ветка в colormapSample().
enum class Colormap {
    Viridis, // перцептивно-равномерная, для потенциала
    Magma,   // перцептивно-равномерная, для тока
};

// Семплирование палитры в точке t∈[0,1] с заданной прозрачностью (alpha 0..255).
// Чистая функция: альфа из LUT (255) переопределяется параметром.
inline uint32_t colormapSample(Colormap which, double t, int alpha) {
    uint32_t c;
    switch (which) {
        case Colormap::Magma: c = lutSample(kMagmaLut, kMagmaLutSize, t); break;
        case Colormap::Viridis:
        default:              c = lutSample(kViridisLut, kViridisLutSize, t); break;
    }
    return withAlpha(c, static_cast<unsigned>(std::clamp(alpha, 0, 255)));
}

inline uint32_t potentialColor(double v, double vMin, double vMax) {
    double range = vMax - vMin;
    // Вырожденный диапазон: возвращаем нейтральный сине-серый (как раньше).
    if (range < 1e-12) return packColor(93, 128, 196, 220);
    double t = std::clamp((v - vMin) / range, 0.0, 1.0);
    return colormapSample(Colormap::Viridis, t, 220);
}

inline uint32_t currentColor(double absI, double maxI) {
    if (maxI < 1e-12) return kMagmaLut[0];
    double t = std::min(absI / maxI, 1.0);
    return lutSample(kMagmaLut, kMagmaLutSize, t);
}

} // namespace current_lab::render
