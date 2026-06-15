#pragma once

namespace current_lab::visualization {

enum class VisualizationLayer {
    Potential,
    Current,
    ElectricField,
    Drift,
    SurfaceCharge,
    MagneticField,
    Heat,
    Power,
    LICField,
};

struct VisualizationStatus {
    const char* name;
    const char* badge;
    const char* model;
    const char* description;
};

inline VisualizationStatus layerStatus(VisualizationLayer layer) {
    switch (layer) {
        case VisualizationLayer::Potential:
            return {"Potential", "approximation",
                    "Node voltages interpolated along a 1D distributed wire model.",
                    "Reasonable in the current quasi-static 1D conductor approximation."};
        case VisualizationLayer::Current:
            return {"Current", "exact-sign / educational-speed",
                    "Branch current from the MNA solution, animated for visibility.",
                    "Direction and magnitude are solver-based; animation speed is pedagogically amplified."};
        case VisualizationLayer::ElectricField:
            return {"E-field", "approximation",
                    "Computed from E ≈ -dV/dx along each conductor.",
                    "Direction is physical in the 1D wire model; surrounding 3D field is not solved."};
        case VisualizationLayer::Drift:
            return {"Drift", "educational",
                    "Electron drift is visualized with amplified speed plus qualitative thermal motion.",
                    "Computed current is untouched; particle motion is a pedagogical overlay."};
        case VisualizationLayer::SurfaceCharge:
            return {"Surface Charge", "heuristic",
                    "Edge samples are derived from sigma ~ (V - Vavg) along each conductor.",
                    "This is not a Maxwell surface-charge solution; it only indicates a plausible sign pattern."};
        case VisualizationLayer::MagneticField:
            return {"Magnetic Field", "qualitative static DC",
                    "Magnitude follows B ~ I/r and glyph polarity follows the right-hand rule.",
                    "Only a local 2D teaching view is shown; no full Biot-Savart geometry is solved."};
        case VisualizationLayer::Heat:
            return {"Heat", "approximation",
                    "Heat intensity comes from dissipated electrical power only.",
                    "Positive resistor/wire power is emphasized; source delivery is excluded from the heat layer."};
        case VisualizationLayer::Power:
            return {"Power", "exact-sign",
                    "Power uses P = I * dV from solver branch results.",
                    "Positive values mean dissipation, negative values mean supplied power."};
        case VisualizationLayer::LICField:
            return {"LIC Field", "approximation",
                    "Line Integral Convolution — directional field texture from white noise "
                    "convolved along E-field streamlines.",
                    "CPU approximation at moderate resolution; GPU/FBO would allow higher density. "
                    "Directional pattern guides the eye along field lines."};
    }
    return {"Unknown", "unknown", "", ""};
}

} // namespace current_lab::visualization
