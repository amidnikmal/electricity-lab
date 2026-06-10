#pragma once

#include "imgui.h"
#include "math/Vec2.h"

struct CanvasCamera {
    Vec2 offset{0.0f, 0.0f};
    float scale = 1.0f;

    ImVec2 worldToScreen(Vec2 world) const {
        return ImVec2(float(world.x * scale + offset.x), float(world.y * scale + offset.y));
    }

    Vec2 screenToWorld(ImVec2 screen) const {
        return Vec2((screen.x - offset.x) / scale, (screen.y - offset.y) / scale);
    }

    void pan(Vec2 delta) { offset = offset + delta; }

    void zoomAt(float factor, Vec2 screenPt) {
        float old = scale;
        scale *= factor;
        if (scale < 0.05f) scale = 0.05f;
        if (scale > 50.0f) scale = 50.0f;
        offset.x = screenPt.x - (scale / old) * (screenPt.x - offset.x);
        offset.y = screenPt.y - (scale / old) * (screenPt.y - offset.y);
    }
};
