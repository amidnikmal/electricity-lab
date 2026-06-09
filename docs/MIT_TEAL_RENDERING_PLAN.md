# MIT TEAL Rendering Plan

## Applied In Second Pass

- calmer dark background and cleaner grid
- preset-driven layer visibility
- explicit legends and layer tooltips
- inspector as probe readout
- pause / speed / reset-time controls
- less decorative magnetic field
- more readable potential palette than HSV rainbow

## Design Direction

Target style is educational instrumentation, not decorative VFX:

- restrained palette
- one dominant message per preset
- vector fields that read as fields
- scalar potential that reads as a scalar map
- overlays that explain themselves

## Still Missing For A Stronger TEAL Feel

- dedicated legend panel for `V`, `E`, `I`, `P`
- probe cursor readout directly on canvas
- render primitive caching for smoother large-wire gradients
- explicit reference-node switching UI
- better typography hierarchy than default ImGui text alone

## Next Visual Upgrade

Move legends and overlay explanations into dedicated render helpers so
`CircuitCanvas` can focus on geometry and interaction only.
