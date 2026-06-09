# Architecture Review

## Summary

Second pass focused on reducing hidden coupling without rewriting the project.

Main improvements:

- `Circuit` no longer assumes `id == vector index`
- `CircuitSolver` now maps node IDs explicitly and supports non-contiguous IDs
- distributed wire mapping uses original component IDs
- pure visualization models were extracted into `src/physics/`
- `MainWindow` owns presets and solver parameters instead of hiding them in canvas code
- `InspectorPanel` now reports solved quantities and model status explicitly

## Current Flow

```text
Circuit
  -> Circuit::toDistributed(DistributedWireParameters)
  -> CircuitSolver
  -> physics sample generation
  -> CircuitCanvas draw primitives
  -> MainWindow / InspectorPanel
```

## What Was Wrong

- `CircuitCanvas`, `InspectorPanel` and `CircuitSolver` used node IDs as vector indices
- `MainWindow::mapDistributedSolution()` relied on component ordering instead of stable IDs
- field, drift, magnetic and surface-charge calculations lived inside rendering code
- distributed wire parameters were hard-coded
- inspector did not expose model assumptions or sign conventions

## What Is Better Now

- node/component IDs remain unique after deletion
- distributed circuits preserve original node/component identity where needed
- `FieldModel`, `DriftModel`, `SurfaceChargeModel`, `MagneticFieldModel` and
  `PowerModel` provide testable pure functions
- toolbar exposes presets, distributed-wire parameters and animation controls
- inspector shows `Va`, `Vb`, `dV`, `I`, `P`, `Length`, distributed `R`, `E`

## Remaining Architectural Debt

- `src/ui/CircuitCanvas.cpp` is still large and mixes:
  - input handling
  - camera
  - draw primitive generation
  - draw submission
- no dedicated `CircuitValidator`
- renderer primitives are still immediate-mode ImGui calls instead of an
  intermediate render list
- solver and distributed-wire transform are still coupled through `Circuit`
  rather than a dedicated builder module

## Recommended Next Extraction

1. Move draw primitive emission into `src/render/`
2. Introduce `CircuitValidator` for missing ground, invalid resistance and bad references
3. Add `VisualizationState` / render-primitive structs for snapshot-style tests
4. Split `CircuitCanvas` into:
   - input/controller
   - renderer coordinator
   - overlay/legend renderer
