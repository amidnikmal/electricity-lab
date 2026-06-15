#pragma once

#include "ui/CanvasCamera.h"
#include "circuit/Circuit.h"
#include <vector>
#include <cstring>
#include <algorithm>

namespace current_lab::render {

inline void flipRowsVertically(std::vector<unsigned char>& pixels, int w, int h, int channels) {
    const int rowBytes = w * channels;
    std::vector<unsigned char> row(rowBytes);
    for (int y = 0; y < h / 2; ++y) {
        unsigned char* top = pixels.data() + y * rowBytes;
        unsigned char* bot = pixels.data() + (h - 1 - y) * rowBytes;
        std::memcpy(row.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, row.data(), rowBytes);
    }
}

inline CanvasCamera computeCameraForCircuit(const Circuit& circuit,
                                             int canvasW, int canvasH,
                                             double padding = 0.15) {
    CanvasCamera cam;
    if (circuit.nodes.empty()) return cam;

    double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
    for (const auto& n : circuit.nodes) {
        minX = std::min(minX, n.position.x);
        maxX = std::max(maxX, n.position.x);
        minY = std::min(minY, n.position.y);
        maxY = std::max(maxY, n.position.y);
    }

    double circuitW = maxX - minX;
    double circuitH = maxY - minY;
    if (circuitW < 1.0) circuitW = 1.0;
    if (circuitH < 1.0) circuitH = 1.0;

    double padX = circuitW * padding;
    double padY = circuitH * padding;
    double worldW = circuitW + 2.0 * padX;
    double worldH = circuitH + 2.0 * padY;

    double scaleX = static_cast<double>(canvasW) / worldW;
    double scaleY = static_cast<double>(canvasH) / worldH;
    double scale = std::min(scaleX, scaleY);

    cam.scale = static_cast<float>(scale);
    cam.offset.x = static_cast<float>(-(minX - padX) * scale);
    cam.offset.y = static_cast<float>(-(minY - padY) * scale);

    return cam;
}

} // namespace current_lab::render
