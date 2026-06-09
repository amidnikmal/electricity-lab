# Visualization Presets

Date: 2026-06-09

Preset state is defined in `src/visualization/VisualizationPresets.h` and is testable without ImGui or OpenGL.

## Circuit

Shows the clean schematic, conventional current, and power readouts for selected elements. Hides potential heatmap, E-field, drift particles, surface charge, magnetic overlays, and debug markers.

## Potential

Shows the potential layer and keeps particle, surface charge, and magnetic overlays hidden. The model is solved circuit voltage plus distributed 1D wire interpolation.

## Electric Field

Shows subdued potential plus E-field arrows. The field is approximated from the voltage gradient along conductive paths (`E ~= -dV/dx` in the distributed 1D model). Magnetic and surface charge overlays are hidden.

## Current / Drift

Shows current/drift visualization with a clear convention readout. Conventional current is the solved branch-current sign; electron drift is educational and opposite conventional current when enabled.

## Power / Heat

Shows current, power labels, and heat/glow tied to dissipated power. Heat is not intended to represent a calibrated thermal simulation.

## Charges

Shows potential, E-field, and surface charge overlay. Surface charge remains heuristic/conceptual and is hidden in other learner presets.

## Debug

Shows all developer overlays and raw controls. This is the only preset that enables debug markers and the persistent log by default.

## Tested Expectations

- Potential preset enables potential and disables drift, surface charge, magnetic, and debug log.
- Electric Field preset enables potential + E-field and disables magnetic/surface/debug markers.
- Circuit preset hides debug markers, surface charge, and magnetic overlays.
- Debug preset enables debug markers, surface charge, magnetic overlay, and debug log.
