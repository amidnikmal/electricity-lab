#pragma once

#include "imgui.h"

namespace current_lab::ui {

// Shows a tooltip with the full text when a label does not fit the widget
// that was just submitted (combos with fixed widths, etc.).
inline void tooltipIfTruncated(const char* label, float widgetWidth) {
    if (!label) return;
    if (ImGui::IsItemHovered() &&
        ImGui::CalcTextSize(label).x > widgetWidth - ImGui::GetFrameHeight() - 6.0f)
        ImGui::SetTooltip("%s", label);
}

} // namespace current_lab::ui
