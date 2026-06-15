#pragma once

#include "imgui.h"
#include "render/RenderPrimitives.h"
#include "ui/CanvasCamera.h"

// Dumb ImGui backend: draws RenderPrimitives, decides nothing about physics.
namespace current_lab::render {

void drawGrid(ImDrawList* dl, const CanvasCamera& camera, ImVec2 origin, ImVec2 size);
// labelScale умножает размер шрифта надписей (читаемость): в приложении = uiScale,
// в офскрин-снимке больше (мелкий дефолтный шрифт тонул на крупной схеме).
void drawPrimitives(ImDrawList* dl, const RenderPrimitives& prims,
                    const CanvasCamera& camera, ImVec2 origin, ImVec2 size,
                    float uiScale = 1.0f, float labelScale = 1.0f);

} // namespace current_lab::render
