#pragma once

#include "math/Vec2.h"
#include <cstdint>
#include <string>
#include <vector>

// Neutral render-primitive layer: pure data, no ImGui. Projection builders emit
// these; the renderer backend draws them. Geometry is in world coordinates.
// Widths/radii are world units unless the screen-space flag says otherwise
// (screen-space sizes stay constant when the camera zooms).
namespace current_lab::render {

// Same channel packing as ImGui's IM_COL32 (R | G<<8 | B<<16 | A<<24).
constexpr uint32_t packColor(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return (r & 0xFFu) | ((g & 0xFFu) << 8) | ((b & 0xFFu) << 16) | ((a & 0xFFu) << 24);
}

constexpr uint32_t withAlpha(uint32_t color, unsigned a) {
    return (color & 0x00FFFFFFu) | ((a & 0xFFu) << 24);
}

struct PrimLine {
    Vec2 a, b;
    double width = 1.0;
    uint32_t color = 0xFFFFFFFFu;
    bool screenSpaceWidth = false;
};

struct PrimPolyline {
    std::vector<Vec2> pts;
    double width = 1.0;
    uint32_t color = 0xFFFFFFFFu;
    bool screenSpaceWidth = true;
};

struct PrimArrow {
    Vec2 pos, dir; // dir is unit-length flow direction
    double size = 6.0;
    uint32_t color = 0xFFFFFFFFu;
};

// Potential gradient strip along a conductor. Carries the physical voltages so
// the colour mapping stays a render concern while the data stays solver-honest.
struct PrimGradient {
    Vec2 a, b;
    double width = 8.0;
    double vA = 0.0, vB = 0.0;
    double vMin = 0.0, vMax = 0.0;
    uint8_t alpha = 255;
};

struct PrimGlow {
    Vec2 center;
    double radius = 10.0;
    double intensity = 1.0; // 0..1
    uint32_t color = 0xFFFFFFFFu;
};

struct PrimParticle {
    Vec2 pos;
    double radius = 2.0;
    uint32_t color = 0xFFFFFFFFu;
    bool screenSpaceRadius = true;
};

struct PrimLabel {
    Vec2 pos;
    std::string text;
    uint32_t color = packColor(200, 200, 200);
    bool debugOnly = false;
};

struct PrimCircle {
    Vec2 center;
    double radius = 5.0;
    uint32_t color = 0xFFFFFFFFu;
    double thickness = 1.5; // outline thickness when not filled (screen px)
    bool filled = false;
    bool screenSpaceRadius = false;
};

struct PrimQuad {
    Vec2 p1, p2, p3, p4;
    uint32_t color = 0xFFFFFFFFu;
    bool filled = true;
    double outlineThickness = 1.5; // screen px when not filled
};

struct PotentialLegend {
    bool show = false;
    double vMin = 0.0, vMax = 0.0;
};

struct RenderPrimitives {
    std::vector<PrimLine> lines;
    std::vector<PrimPolyline> polylines;
    std::vector<PrimArrow> arrows;
    std::vector<PrimGradient> gradients;
    std::vector<PrimGlow> glows;
    std::vector<PrimParticle> particles;
    std::vector<PrimLabel> labels;
    std::vector<PrimCircle> circles;
    std::vector<PrimQuad> quads;
    PotentialLegend legend;

    void clear() {
        lines.clear(); polylines.clear(); arrows.clear(); gradients.clear();
        glows.clear(); particles.clear(); labels.clear(); circles.clear(); quads.clear();
        legend = PotentialLegend{};
    }

    size_t totalCount() const {
        return lines.size() + polylines.size() + arrows.size() + gradients.size() +
               glows.size() + particles.size() + labels.size() + circles.size() + quads.size();
    }
};

} // namespace current_lab::render
