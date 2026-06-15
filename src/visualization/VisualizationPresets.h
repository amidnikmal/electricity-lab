#pragma once

namespace current_lab::visualization
{

enum class VisualizationPreset
{
    Circuit = 0,
    Potential,
    ElectricField,
    CurrentDrift,
    PowerHeat,
    Charges,
    Debug,
    Count
};

struct LayerVisibility
{
    bool current = false;
    bool electronFlow = false;
    bool potential = false;
    bool drift = false;
    bool electricField = false;
    bool heat = false;
    bool power = false;
    bool magnetic = false;
    bool surfaceCharge = false;
    bool lic = false;
    bool canvasReadouts = false;
    bool debugMarkers = false;
    bool debugLog = false;
};

struct PresetInfo
{
    VisualizationPreset preset = VisualizationPreset::Circuit;
    const char* label = "Circuit";
    const char* modelNote = "Lumped circuit view.";
    LayerVisibility layers;
};

inline const char* presetLabel(VisualizationPreset preset)
{
    switch (preset)
    {
    case VisualizationPreset::Circuit: return "Circuit";
    case VisualizationPreset::Potential: return "Potential";
    case VisualizationPreset::ElectricField: return "Electric Field";
    case VisualizationPreset::CurrentDrift: return "Current / Drift";
    case VisualizationPreset::PowerHeat: return "Power / Heat";
    case VisualizationPreset::Charges: return "Charges";
    case VisualizationPreset::Debug: return "Debug";
    case VisualizationPreset::Count: break;
    }
    return "Circuit";
}

inline PresetInfo presetInfo(VisualizationPreset preset)
{
    PresetInfo info;
    info.preset = preset;
    info.label = presetLabel(preset);

    switch (preset)
    {
    case VisualizationPreset::Circuit:
        info.modelNote = "Clean circuit schematic; values appear in inspector/readouts.";
        info.layers.current = true;
        info.layers.power = true;
        info.layers.canvasReadouts = true;
        return info;
    case VisualizationPreset::Potential:
        info.modelNote = "Potential from solved circuit and distributed 1D wire interpolation.";
        info.layers.potential = true;
        return info;
    case VisualizationPreset::ElectricField:
        info.modelNote = "E-field approximated as -dV/dx along conductive paths.";
        info.layers.potential = true;
        info.layers.electricField = true;
        info.layers.lic = true;
        return info;
    case VisualizationPreset::CurrentDrift:
        info.modelNote = "Conventional current is physical sign; drift animation is educational.";
        info.layers.current = true;
        info.layers.drift = true;
        info.layers.canvasReadouts = true;
        return info;
    case VisualizationPreset::PowerHeat:
        info.modelNote = "Heat/glow follows dissipated power P = I*dV.";
        info.layers.current = true;
        info.layers.heat = true;
        info.layers.power = true;
        info.layers.canvasReadouts = true;
        return info;
    case VisualizationPreset::Charges:
        info.modelNote = "Surface charge overlay is heuristic/conceptual.";
        info.layers.potential = true;
        info.layers.electricField = true;
        info.layers.surfaceCharge = true;
        info.layers.lic = true;
        return info;
    case VisualizationPreset::Debug:
        info.modelNote = "Developer view with raw overlays and logs.";
        info.layers.current = true;
        info.layers.potential = true;
        info.layers.drift = true;
        info.layers.electricField = true;
        info.layers.heat = true;
        info.layers.power = true;
        info.layers.magnetic = true;
        info.layers.surfaceCharge = true;
        info.layers.lic = true;
        info.layers.canvasReadouts = true;
        info.layers.debugMarkers = true;
        info.layers.debugLog = true;
        return info;
    case VisualizationPreset::Count:
        break;
    }

    info.preset = VisualizationPreset::Circuit;
    info.label = presetLabel(VisualizationPreset::Circuit);
    info.modelNote = "Clean circuit schematic; values appear in inspector/readouts.";
    info.layers.current = true;
    info.layers.power = true;
    info.layers.canvasReadouts = true;
    return info;
}

// The preset fully drives the physics-pane layers, so switching presets is
// always a visible change. The startup default is Current / Drift, which has
// the animated layers on out of the box.
constexpr VisualizationPreset kDefaultPreset = VisualizationPreset::CurrentDrift;

inline bool isLearnerPreset(VisualizationPreset preset)
{
    return preset != VisualizationPreset::Debug && preset != VisualizationPreset::Count;
}

inline bool isDebugPreset(VisualizationPreset preset)
{
    return preset == VisualizationPreset::Debug;
}

} // namespace current_lab::visualization
