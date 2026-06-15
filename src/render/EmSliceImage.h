#pragma once
//
// EmSliceImage — рендер 2D-среза 3D ЭМ-поля (FdtdField) в RGBA-картинку (viridis).
//
// Чистая функция без GL/UI: возвращает пиксели RGBA8 (байтовый порядок R,G,B,A —
// как packColor), пригодные и для GL-текстуры (ImGui::Image), и для PNG (--capture).
// Объёмный рендер не делаем — показываем ортогональные срез-плоскости (см. EM_FDTD_PLAN).

#include "physics/FdtdField.h"
#include "render/ColorMaps.h"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace current_lab::render {

enum class EmPlane { XY, XZ, YZ };          // плоскость среза (фиксируется третья ось)
enum class EmFieldView { EzSigned, EMag };  // знаковое Ez (волна) или |E| (интенсивность)

struct EmImage {
    int w = 0, h = 0;
    std::vector<uint32_t> rgba;             // w*h, RGBA8 (R — младший байт)
    const unsigned char* bytes() const {
        return reinterpret_cast<const unsigned char*>(rgba.data());
    }
};

// Срез поля sim в плоскости plane на индексе фиксированной оси slice.
// scale — нормировка: для EzSigned значение делится на scale и кладётся в [-1,1]
// (t=0.5 — ноль), для EMag — в [0,1].
inline EmImage renderEmSlice(const physics::FdtdField& sim, EmPlane plane, int slice,
                             EmFieldView which, double scale) {
    const physics::FdtdField& s = sim;
    int w = 0, h = 0;
    switch (plane) {
        case EmPlane::XY: w = s.nx(); h = s.ny(); break;
        case EmPlane::XZ: w = s.nx(); h = s.nz(); break;
        case EmPlane::YZ: w = s.ny(); h = s.nz(); break;
    }
    EmImage img; img.w = w; img.h = h; img.rgba.assign(static_cast<size_t>(w) * h, 0);
    const double invScale = (scale > 1e-30) ? 1.0 / scale : 0.0;

    for (int b = 0; b < h; ++b) {
        for (int a = 0; a < w; ++a) {
            int i = 0, j = 0, k = 0;
            switch (plane) {
                case EmPlane::XY: i = a; j = b; k = slice; break;
                case EmPlane::XZ: i = a; j = slice; k = b; break;
                case EmPlane::YZ: i = slice; j = a; k = b; break;
            }
            double t;
            if (which == EmFieldView::EzSigned) {
                double v = std::clamp(static_cast<double>(s.ez(i, j, k)) * invScale, -1.0, 1.0);
                t = 0.5 + 0.5 * v;
            } else {
                t = std::clamp(s.eMag(i, j, k) * invScale, 0.0, 1.0);
            }
            img.rgba[static_cast<size_t>(b) * w + a] = colormapSample(Colormap::Viridis, t, 255);
        }
    }
    return img;
}

} // namespace current_lab::render
