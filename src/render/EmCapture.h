#pragma once
//
// EmCapture — headless-снимок ЭМ-сцены (FDTD) в PNG. БЕЗ GL: поле считается на CPU,
// срез рендерится renderEmSlice() в RGBA, запись через stb. Для иллюстраций практикума
// и регрессии (как circuit-капчер, но без GLFW-окна).

#include "physics/FdtdField.h"
#include "physics/EmScene.h"
#include "render/EmSliceImage.h"
#include "third_party/stb_image_write.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace current_lab::render {

struct EmCaptureResult {
    bool ok = false;
    std::string error;
    int width = 0, height = 0;
};

// demo — сцена; grid — размер куба сетки; steps — шагов интегрирования;
// plane/field — что показывать; out — путь PNG; upscale — целочисленное увеличение
// (nearest) для более крупной картинки.
inline EmCaptureResult captureEmToPng(physics::EmDemo demo, int grid, int steps,
                                      EmPlane plane, EmFieldView field,
                                      const std::string& out, int upscale = 6) {
    EmCaptureResult r;
    if (demo == physics::EmDemo::Count) { r.error = "unknown EM demo"; return r; }
    if (grid < 8 || steps < 1) { r.error = "bad grid/steps"; return r; }

    physics::FdtdConfig cfg; cfg.nx = cfg.ny = cfg.nz = grid;
    physics::FdtdField sim(cfg);
    physics::EmSource src = physics::buildEmScene(sim, demo);
    for (int n = 0; n < steps; ++n) { physics::injectEmSource(sim, src, n); sim.step(); }

    const int slice = (plane == EmPlane::XY ? sim.nz()
                     : plane == EmPlane::XZ ? sim.ny() : sim.nx()) / 2;

    // Авто-нормировка по ПЕРЦЕНТИЛЮ (а не максимуму): у точечного источника
    // ближнее поле (~1/r³) на порядки больше излучённого, и максимум «съел» бы
    // всю палитру. 97-й перцентиль |поля| даёт контраст волн, источник насыщается.
    int w = (plane == EmPlane::YZ ? sim.ny() : sim.nx());
    int h = (plane == EmPlane::XY ? sim.ny() : sim.nz());
    std::vector<double> vals;
    vals.reserve(static_cast<size_t>(w) * h);
    for (int b = 0; b < h; ++b)
        for (int a = 0; a < w; ++a) {
            int i = 0, j = 0, k = 0;
            switch (plane) {
                case EmPlane::XY: i = a; j = b; k = slice; break;
                case EmPlane::XZ: i = a; j = slice; k = b; break;
                case EmPlane::YZ: i = slice; j = a; k = b; break;
            }
            double v = (field == EmFieldView::EzSigned)
                         ? std::fabs(static_cast<double>(sim.ez(i, j, k)))
                         : sim.eMag(i, j, k);
            vals.push_back(v);
        }
    double scale = 1e-30;
    if (!vals.empty()) {
        size_t q = static_cast<size_t>(0.97 * (vals.size() - 1));
        std::nth_element(vals.begin(), vals.begin() + q, vals.end());
        scale = std::max(scale, vals[q]);
    }

    EmImage img = renderEmSlice(sim, plane, slice, field, scale);

    // Nearest-апскейл.
    const int u = std::max(1, upscale);
    const int W = img.w * u, H = img.h * u;
    std::vector<uint32_t> big(static_cast<size_t>(W) * H);
    for (int b = 0; b < H; ++b)
        for (int a = 0; a < W; ++a)
            big[static_cast<size_t>(b) * W + a] = img.rgba[static_cast<size_t>(b / u) * img.w + (a / u)];

    if (!stbi_write_png(out.c_str(), W, H, 4, big.data(), W * 4)) {
        r.error = "stbi_write_png failed for: " + out;
        return r;
    }
    r.ok = true; r.width = W; r.height = H;
    return r;
}

} // namespace current_lab::render
