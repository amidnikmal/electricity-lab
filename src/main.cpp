#include "app/App.h"
#include "app/CaptureConfig.h"
#include "render/CaptureRenderer.h"
#include "render/EmCapture.h"
#include "render/EmSliceImage.h"
#include "physics/EmScene.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    auto cfg = current_lab::app::parseCaptureArgs(argc, argv);

    // ЭМ-захват (FDTD) — чистый CPU, без GL-контекста.
    if (cfg.emCapture) {
        if (cfg.outputFile.empty()) {
            std::fprintf(stderr,
                "Usage: current-lab --em <DipoleRadiator|PlaneWave|DoubleSlit|"
                "DielectricInterface|Mirror|Waveguide> --out <file.png> "
                "[--grid N] [--steps N] [--plane xy|xz|yz] [--field ez|emag]\n");
            return 1;
        }
        using namespace current_lab;
        render::EmPlane plane = cfg.emPlane == "xz" ? render::EmPlane::XZ
                              : cfg.emPlane == "yz" ? render::EmPlane::YZ
                                                    : render::EmPlane::XY;
        render::EmFieldView fview = (cfg.emField == "emag")
                              ? render::EmFieldView::EMag : render::EmFieldView::EzSigned;
        auto res = render::captureEmToPng(physics::emDemoFromName(cfg.emDemo),
                                          cfg.emGrid, cfg.emSteps, plane, fview,
                                          cfg.outputFile);
        if (!res.ok) { std::fprintf(stderr, "EM capture failed: %s\n", res.error.c_str()); return 1; }
        std::printf("Captured EM %dx%d PNG to %s\n", res.width, res.height, cfg.outputFile.c_str());
        return 0;
    }

    if (cfg.capture) {
        if (cfg.demoName.empty() || cfg.outputFile.empty()) {
            std::fprintf(stderr,
                         "Usage: current-lab --capture --demo <DemoName> "
                         "--out <file.png> [--view electrical|mechanical|hydraulic] "
                         "[--layers potential,current,heat,...] [--width N] [--height N] "
                         "[--time T]\n");
            return 1;
        }

        App app;
        if (!app.initOffscreen(cfg.width, cfg.height)) {
            std::fprintf(stderr, "Failed to initialize offscreen GL context\n");
            return 1;
        }

        auto result = current_lab::render::captureToPng(
            cfg.width, cfg.height,
            cfg.demoName,
            current_lab::app::parseView(cfg.view),
            current_lab::app::parseLayers(cfg.layers),
            cfg.time,
            cfg.outputFile);

        if (!result.ok) {
            std::fprintf(stderr, "Capture failed: %s\n", result.error.c_str());
            return 1;
        }

        std::printf("Captured %dx%d PNG to %s\n", result.width, result.height,
                    cfg.outputFile.c_str());
        return 0;
    }

    App app;
    if (!app.init()) {
        std::fprintf(stderr, "Failed to initialize application\n");
        return 1;
    }
    app.run();
    return 0;
}
