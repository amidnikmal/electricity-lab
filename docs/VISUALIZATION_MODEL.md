# Visualization Model

## Pure Sample Types

Current visualization models are expressed as pure sample generators:

```text
FieldArrowSample[]
DriftParticleState[]
SurfaceChargeSample[]
MagneticFieldSample[]
```

These types live in `src/physics/` and are consumed by `CircuitCanvas`.

## Layer Notes

### Potential

- Source: solved node potentials
- Rendering: conductor interior gradient
- Status: approximation

### Electric Field

- Source: `FieldModel`
- Equation: `E ~= -dV/dx`
- Status: approximation

### Drift

- Source: `DriftModel`
- Meaning:
  - computed current is solver truth
  - particle motion is pedagogical
  - thermal motion is qualitative
  - visual speed is amplified

### Surface Charge

- Source: `SurfaceChargeModel`
- Meaning:
  - plausible sign distribution along conductor surface
  - not a Maxwell or Poisson solve

### Magnetic Field

- Source: `MagneticFieldModel`
- Meaning:
  - local quasi-static `B ~ I/r`
  - page-normal glyphs follow right-hand rule
  - not a full 3D Biot-Savart integration

### Heat / Power

- Source: solver branch power + `PowerModel`
- Meaning:
  - heat layer uses dissipated power only
  - source supply remains visible in power labels, not as heat

## UI Communication Rules

- Each layer toggle has a tooltip with status and model summary
- Inspector shows model status for the selected element
- Electron-flow convention and visual speed are exposed instead of being implicit
