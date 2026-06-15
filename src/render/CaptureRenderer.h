#pragma once

#include "render/RenderPrimitives.h"
#include "visualization/VisualizationPresets.h"
#include "projection/ProjectionBuilder.h"
#include <string>

namespace current_lab::render {

struct CaptureResult {
    bool ok = false;
    std::string error;
    int width = 0;
    int height = 0;
};

CaptureResult captureToPng(int width, int height,
                           const std::string& demoName,
                           projection::ProjectionKind view,
                           const visualization::LayerVisibility& layers,
                           double simTime,
                           const std::string& outputPath);

} // namespace current_lab::render
