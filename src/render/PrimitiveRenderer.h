#pragma once

#include "imgui.h"
#include "render/RenderPrimitives.h"
#include "ui/CanvasCamera.h"

// Dumb ImGui backend: draws RenderPrimitives, decides nothing about physics.
namespace current_lab::render {

void drawGrid(ImDrawList* dl, const CanvasCamera& camera, ImVec2 origin, ImVec2 size);
void drawPrimitives(ImDrawList* dl, const RenderPrimitives& prims,
                    const CanvasCamera& camera, ImVec2 origin, ImVec2 size);

} // namespace current_lab::render
