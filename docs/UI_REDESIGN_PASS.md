# UI Redesign Pass

Date: 2026-06-09

## Goal

Move Current Lab from a debug/prototype interface toward a calm educational desktop app for DC circuit visualization. This pass keeps the solver and physical model intact and focuses on presentation, hierarchy, and layer defaults.

## Layout

The main window now follows this structure:

```text
Top bar: Current Lab | preset | Run Solver | Pause | Debug
Tool rail | Main Physics Canvas | Right Inspector
Bottom analysis strip
```

The canvas remains the primary surface. Editing tools live in the left rail. Visualization state, solved values, selected element details, and simulation controls live in the right inspector. The always-visible debug log is removed from Learner Mode and appears in Debug Mode only.

## Learner Mode Defaults

Learner Mode is any preset except Debug. It hides raw node markers, node labels, dense node voltage labels, raw overlay controls, magnetic overlays, and surface charge samples unless the selected preset explicitly calls for them.

Selected nodes/components can still show compact readouts on the canvas when the preset allows it, while full numerical data is shown in the right inspector and bottom strip.

## Debug Mode

Debug preset enables developer-oriented layers and raw layer switches:

- node/readout markers
- potential
- E-field
- current and drift
- heat/power
- magnetic overlay
- surface charge overlay
- debug log
- verbose inspector

## Current Limitations

- The Probe tool is represented as selected-element readout in this pass, not a separate movable probe interaction.
- The old inspector still exists as a verbose Debug Mode section.
- Some rendering code still lives inside `CircuitCanvas`; only preset state was extracted in this pass.
