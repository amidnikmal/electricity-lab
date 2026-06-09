# Current Lab

Интерактивное C++ / OpenGL / Dear ImGui приложение для изучения тока, потенциала,
напряжённости поля, дрейфа электронов и рассеяния энергии в простых цепях.

Проект ориентирован на учебную физическую честность:

- solver даёт схемное DC-решение через MNA;
- distributed wire даёт 1D-приближение конечного сопротивления провода;
- визуальные слои помечены как `exact-sign`, `approximation`, `educational`,
  `heuristic` или `qualitative quasi-static`;
- renderer больше не придумывает E-field / drift / surface charge / B-field
  прямо внутри UI-логики: эти слои вынесены в чистые модели в `src/physics/`.

## Quick Start

```bash
cd current-lab
cmake -S . -B build
cmake --build build -j$(nproc)
./build/current-lab-tests
DISPLAY=:0 ./build/current-lab
```

## Current Scope

- Circuit editor: node, wire, resistor, voltage source, ground
- DC steady-state solver: modified nodal analysis
- Distributed wire mode: configurable `segments` and `R / unit`
- Potential gradient on conductors
- Current arrows with sign-correct branch current
- E-field layer with `E ~= -dV/dx` along wires
- Drift particles with explicit note that visual speed is amplified
- Surface charge layer explicitly marked as heuristic
- Magnetic field layer updated to `B ~ I/r` page-normal glyphs, marked qualitative
- Heat and power layers with dissipation vs supply sign convention
- Inspector with `Va`, `Vb`, `dV`, `I`, `P`, `Length`, distributed `R`, `E`
- Visual presets:
  - Circuit view
  - Potential view
  - E-field view
  - Electron drift view
  - Power/heat view
  - Surface charge view
  - Full educational overlay
- Animation controls:
  - pause
  - speed slider
  - reset time

## Physical Status Of Layers

| Layer | Status | Notes |
|---|---|---|
| Solver current / voltage / power signs | `exact-sign` | From MNA solution |
| Potential | `approximation` | Interpolated along distributed 1D wire model |
| Electric field | `approximation` | `E ~= -dV/dx` along each conductor |
| Drift | `educational` | Direction logic preserved, speed amplified for visibility |
| Surface charge | `heuristic` | `sigma ~ (V - Vavg)` with junction-strength boost |
| Magnetic field | `qualitative quasi-static` | `B ~ I/r`, right-hand-rule page glyphs |
| Heat | `approximation` | Dissipated power only |

## Architecture

Current data flow:

```text
Circuit
  -> Circuit::toDistributed(...)
  -> CircuitSolver
  -> physics/* pure visualization models
  -> CircuitCanvas renderer
  -> MainWindow / InspectorPanel
```

Key files:

```text
src/
  circuit/Circuit.h
  solver/CircuitSolver.h/.cpp
  physics/
    PhysicalUnits.h
    WirePhysics.h
    FieldModel.h
    DriftModel.h
    SurfaceChargeModel.h
    MagneticFieldModel.h
    PowerModel.h
  visualization/VisualizationStatus.h
  ui/
    MainWindow.h/.cpp
    CircuitCanvas.h/.cpp
    InspectorPanel.h/.cpp
```

Important second-pass changes:

- non-contiguous `node.id` and `component.id` are handled explicitly instead of
  assuming `id == vector index`;
- distributed-wire source mapping uses original component IDs, not vector offsets;
- `CircuitCanvas` now consumes pure model samples for field, drift, magnetic and
  surface-charge layers;
- inspector exposes model status and per-element physical quantities.

## Tests

The test suite covers:

- circuit graph operations and ID stability
- solver correctness, sign conventions and distributed wire behaviour
- consistency checks (KCL / KVL / power balance / Tellegen)
- canvas state and geometry
- pure visualization model behaviour for field, drift, magnetic field and
  surface charge

## Known Limitations

- No transient simulation, capacitance or inductance
- No full Maxwell / Laplace / Poisson field solve
- Surface charge remains heuristic
- Magnetic field remains a local qualitative teaching overlay, not a full 3D solve
- Potential is still referenced to the chosen ground; arbitrary reference switching
  is not yet exposed in UI
- Temperature evolution is not simulated; heat is power-based only
- Save/load and undo/redo are still absent

## Documentation

- [docs/HANDOFF.md](docs/HANDOFF.md)
- [docs/model_assumptions.md](docs/model_assumptions.md)
- [docs/electricity_model_notes.md](docs/electricity_model_notes.md)
- [docs/ARCHITECTURE_REVIEW.md](docs/ARCHITECTURE_REVIEW.md)
- [docs/PHYSICS_AUDIT.md](docs/PHYSICS_AUDIT.md)
- [docs/VISUALIZATION_MODEL.md](docs/VISUALIZATION_MODEL.md)
- [docs/TEST_PLAN.md](docs/TEST_PLAN.md)
- [docs/MIT_TEAL_RENDERING_PLAN.md](docs/MIT_TEAL_RENDERING_PLAN.md)
- [docs/SECOND_PASS_REPORT.md](docs/SECOND_PASS_REPORT.md)
