#pragma once

#include "circuit/DemoCircuits.h"
#include "projection/ProjectionBuilder.h"
#include "visualization/VisualizationPresets.h"
#include <string>
#include <cstring>

namespace current_lab::app {

struct CaptureConfig {
    bool capture = false;
    std::string demoName;
    std::string view;
    std::string layers;
    std::string outputFile;
    int width = 1600;
    int height = 1000;
    double time = -1.0;  // -1 = run to settle

    // ---- ЭМ-захват (FDTD, отдельный движок; см. --em) ----
    bool   emCapture = false;
    std::string emDemo;          // PlaneWave|DipoleRadiator|DoubleSlit|...
    int    emGrid = 140;         // размер куба сетки
    int    emSteps = 260;        // шагов интегрирования
    std::string emPlane = "xy";  // плоскость среза
    std::string emField = "ez";  // ez (знаковое) | emag (|E|)
};

inline demos::DemoCircuit parseDemoName(const std::string& name) {
    using DC = demos::DemoCircuit;
    struct Entry { const char* key; DC value; };
    // TODO: think of a map later -- but for now, since we only need at most
    // 12 entries one day, a linear scan is perfectly fine.
    static const Entry table[] = {
        {"SourceResistor",   DC::SourceResistor},
        {"ResistorDivider",  DC::ResistorDivider},
        {"RcCapacitor",      DC::RcCapacitor},
        {"RlInductor",       DC::RlInductor},
        {"DiodeResistor",    DC::DiodeResistor},
        {"SwitchResistor",   DC::SwitchResistor},
        {"SwitchedRc",       DC::SwitchedRc},
        {"RlcSeries",        DC::RlcSeries},
        {"RlcCirculating",   DC::RlcCirculating},
        {"PeakDetector",     DC::PeakDetector},
        {"AcRectifier",      DC::AcRectifier},
        {"WheatstoneBridge", DC::WheatstoneBridge},
        {"LoadedDivider",    DC::LoadedDivider},
        {"RcLowPassAc",      DC::RcLowPassAc},
        {"RlcBandPassAc",    DC::RlcBandPassAc},
        {"Superposition",    DC::Superposition},
        {"LadderR",          DC::LadderR},
    };
    for (const auto& e : table) {
        if (name == e.key) return e.value;
    }
    // Fallback: try matching the display name returned by demoName()
    for (int i = 0; i < static_cast<int>(DC::Count); ++i) {
        auto d = static_cast<DC>(i);
        if (name == demos::demoName(d)) return d;
    }
    return DC::Count; // invalid
}

inline projection::ProjectionKind parseView(const std::string& view) {
    if (view == "mechanical") return projection::ProjectionKind::Mechanical;
    if (view == "hydraulic")  return projection::ProjectionKind::Hydraulic;
    return projection::ProjectionKind::Physics; // "electrical" or default
}

inline visualization::LayerVisibility parseLayers(const std::string& list) {
    visualization::LayerVisibility lv{};
    if (list.empty()) {
        lv.current = true;
        lv.potential = true;
        lv.canvasReadouts = true;
        return lv;
    }
    std::string token;
    for (size_t i = 0; i <= list.size(); ++i) {
        if (i == list.size() || list[i] == ',') {
            if (token == "current") lv.current = true;
            else if (token == "electronFlow") lv.electronFlow = true;
            else if (token == "potential") lv.potential = true;
            else if (token == "drift") lv.drift = true;
            else if (token == "electricField") lv.electricField = true;
            else if (token == "heat") lv.heat = true;
            else if (token == "power") lv.power = true;
            else if (token == "magnetic") lv.magnetic = true;
            else if (token == "surfaceCharge") lv.surfaceCharge = true;
            else if (token == "lic") lv.lic = true;
            else if (token == "canvasReadouts") lv.canvasReadouts = true;
            else if (token == "debugMarkers") lv.debugMarkers = true;
            token.clear();
        } else {
            token += list[i];
        }
    }
    return lv;
}

inline CaptureConfig parseCaptureArgs(int argc, char* argv[]) {
    CaptureConfig cfg;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--capture") == 0) {
            cfg.capture = true;
        } else if (std::strcmp(argv[i], "--demo") == 0 && i + 1 < argc) {
            cfg.demoName = argv[++i];
        } else if (std::strcmp(argv[i], "--view") == 0 && i + 1 < argc) {
            cfg.view = argv[++i];
        } else if (std::strcmp(argv[i], "--layers") == 0 && i + 1 < argc) {
            cfg.layers = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            cfg.outputFile = argv[++i];
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            cfg.width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            cfg.height = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            cfg.time = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--em") == 0 && i + 1 < argc) {
            cfg.emCapture = true; cfg.emDemo = argv[++i];
        } else if (std::strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            cfg.emSteps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--grid") == 0 && i + 1 < argc) {
            cfg.emGrid = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--plane") == 0 && i + 1 < argc) {
            cfg.emPlane = argv[++i];
        } else if (std::strcmp(argv[i], "--field") == 0 && i + 1 < argc) {
            cfg.emField = argv[++i];
        }
    }
    return cfg;
}

} // namespace current_lab::app
