#include "app/App.h"
#include "app/CaptureConfig.h"
#include "render/CaptureRenderer.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    auto cfg = current_lab::app::parseCaptureArgs(argc, argv);

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
