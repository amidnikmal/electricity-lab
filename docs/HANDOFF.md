# Current Lab — Agent Handoff

## Project overview

Интерактивное приложение для построения и анализа электрических схем с
физически-корректной визуализацией полей и дрейфа электронов внутри проводов.

**Текущее состояние**: приложение запускается, умеет редактировать схему (узлы, провода, резисторы, источник напряжения, земля), решать MNA (Modified Nodal Analysis) **с всегда включённой распределённой моделью провода** (каждый Wire → 8 резисторных сегментов), визуализировать потенциал, ток, E-поле, дрейф электронов, поверхностные заряды, магнитное поле, тепловыделение и мощность. Провода — физические 2D-объекты с толщиной в мировых единицах (по умолчанию 8 wu), масштабируемой с zoom'ом. Градиент потенциала заполняет всё сечение провода (как цветовая карта в CFD/тепловых симуляциях).

## Quick commands

```bash
cd /home/dima/Desktop/electricity/current-lab

# Build (both app + tests)
cmake --build build -j$(nproc)

# Tests (138 tests, 10 suites, all pass)
./build/current-lab-tests

# Run (Wayland-only, must set DISPLAY)
DISPLAY=:0 ./build/current-lab

# Background (for current session)
# Uses background_process tool with DISPLAY=:0 build/current-lab
```

## Architecture

```
current-lab/
├── CMakeLists.txt          # C++20, FetchContent: GLFW 3.4, GLM 1.0.1, ImGui v1.91.7-docking, GTest v1.14.0
├── src/
│   ├── main.cpp            # GLFW + OpenGL 3.3 COMPAT + ImGui init, main loop → MainWindow::render()
│   ├── gl_setup.h          # OpenGL loader (glad/gl3w built-in)
│   ├── app/                # App lifecycle (minimal)
│   ├── circuit/
│   │   └── Circuit.h       # Node, Component, Circuit struct — no .cpp (header-only)
│   │                       # ComponentType: Wire/Resistor/VoltageSource/Ground
│   │                       # Circuit::toDistributed(N) — splits Wire into N resistor segments
│   ├── solver/
│   │   └── CircuitSolver.h/cpp  # MNA solver, builds linear system, solves via Gauss-Jordan
│   │                       # Output: CircuitSolution { nodePotentials, branches }
│   ├── math/
│   │   ├── Vec2.h          # 2D vector (double), operator overloads
│   │   └── LinearSystem.h  # Gauss-Jordan elimination
│   ├── ui/
│   │   ├── MainWindow.h/cpp    # Top-level layout (3-column: toolbar | canvas | inspector), toolbar with checkboxes
│   │   ├── CircuitCanvas.h/cpp # Canvas rendering: draws nodes, components, fields, particles
│   │   │                       # 898 lines — the core rendering module
│   │   ├── InspectorPanel.h/cpp # Properties panel + log window
│   │   └── Format.h            # milliamps(), milliwatts() formatting
│   └── render/ simulation/     # Empty — placeholders for future modules
├── tests/
│   ├── test_canvas.cpp         # 530 lines — canvas state, camera, wire geometry, visualization toggles
│   ├── test_circuit.cpp        # Node/component add/remove, graph integrity
│   ├── test_solver.cpp         # MNA solver correctness, KCL, Ohm's law
│   ├── test_consistency.cpp    # Solver idempotency, circuit invariants
│   └── test_linear_system.cpp  # Gauss-Jordan basics
└── docs/
    ├── electricity_model_notes.md
    ├── model_assumptions.md
    └── HANDOFF.md              # ← this file
```

### Data flow

```
MainWindow.render()
  ├── canvas.setMode/show/... (sync toolbar state)
  ├── canvas.render(circuit, solution)
  │     └── for each component:
  │           drawWire / drawResistor / drawVoltageSource / drawGround
  │           drawEFieldArrows / drawMagneticField / drawCurrentArrows
  │           drawDriftParticles / drawSurfaceCharge / drawPotentialLegend
  ├── inspector.render(circuit, solution)
  └── renderLog()
```

### Key types

- **Vec2**: `double` x,y — all world coordinates use double
- **CanvasCamera**: offset + scale (0.05x–50x), `worldToScreen()` / `screenToWorld()`
- **CanvasCallbacks**: `std::function` callbacks for wiring canvas actions to circuit model
- **CircuitSolution**: `std::vector<SolutionPoint>` (nodeId→potential), `std::vector<BranchResult>` (componentId→current, voltageDrop, power)

## Feature inventory

### Core editing
- [x] Node placement with drag
- [x] Wire, Resistor, VoltageSource, Ground placement (click two nodes)
- [x] Delete (Del key) — component or node with connected components
- [x] Mode switching (Select, PlaceNode, PlaceWire, PlaceResistor, PlaceVoltageSource, PlaceGround)
- [x] Pan (middle-drag), zoom (scroll, 0.05x–50x clamped)
- [x] Wire length display, potential labels at nodes
- [x] "Run Solver", "Clear Circuit", "Reset Demo" buttons

### Solver (MNA)
- [x] DC steady-state linear solver
- [x] Gaussian elimination with partial pivoting
- [x] Ground node handling (one ground per circuit)
- [x] Distributed wire model (`toDistributed(8)`) — splits Wire into 8 resistor segments
- [x] Branch results: current, voltage drop, power for each component
- [x] Solver idempotency verified by tests

### Physics visualization (all on CircuitCanvas)
| Feature | Toggle | Default | Description |
|---|---|---|---|
| Current arrows | Show Current | ON | Animated green→red arrows (size ∝ current) |
| Electron flow | Electron Flow | OFF | Reverses arrow/particle direction |
| Potential gradient | Show Potential | ON | Full cross-section HSV color map (rainbow: blue=low, red=high) — wire as filled quad with N gradient strips |
| Drift particles | Show Drift | ON | Orange/blue particles with thermal motion + drift velocity, count ∝ wire volume (volume/40, max 1200) |
| E-field arrows | Show E-field | ON | Green arrows on centerline, **spread across cross-section at high zoom** (≤5 rows, CFD-style vector field) |
| Heat map | Show Heat | ON | Glow line along resistors (width ∝ power dissipation) |
| Power display | Show Power | ON | P=…mW text labels on components |
| Magnetic field | Show Magnetic | OFF | Concentric rings (⊗/⊙) around current-carrying wires (radii ∝ wire width, intensity ∝ current) |
| Surface charges | Surface Charge | ON | Red/blue dots at wire edges (σ ∝ V − V_avg), visible at any zoom |

### Wire as physical conductor
- Default thickness: 8.0 wu (world units), slider range [2.0, 50.0] wu
- `wireScreenWidth() = m_wireThickness * m_camera.scale` — scales proportionally with zoom
- Wire body: filled dark quad (RGB 38,42,50), green outline (120,180,120)
- Gradient strips: full halfB-wide quads (not 72% core), N capped at 1000, visible when `screenW > 1.0f`
- Rounded endpoint circles (outlines only, not filled — filled circles caused "dead zone" visual bug at wire ends)
- Surface charge dots: offset `halfW * 0.92f` from centerline, red = positive σ, blue = negative σ, dot radius ∝ screenW
- E-field arrows: at `screenW > 24px`, drawn in up to 5 rows across wire cross-section

### Wire connection rounding (drawWire/drawLead/drawBar)
- `AddCircle` outline at each endpoint with radius `screenHW = halfW * scale` and outline color `(120,180,120,1.5f)`
- `AddQuad` outline along the body edges
- Combined effect: pill-shaped (rounded rectangle) wire appearance at junctions
- **No AddCircleFilled** — these were removed after causing visible dark semicircular "cut-outs" beyond the gradient quads

### Tests: 138 tests, 10 suites, all pass
- `CanvasModeSwitch` (5 tests): mode-clears-drag/place behavior
- `CanvasVisualization` (38 tests): toggle defaults, wire thickness, wire screen width, surface charge geometry, particle radius, no-dead-zone verification
- `CanvasCamera` (4): world↔screen roundtrip, zoom clamp, pan accumulation
- `CircuitModel` (2): click-without-drag invariance, node move
- `CanvasPlacement` (9): component creation, placement, callback wiring
- `SolverIdempotent` (1): re-solving same circuit = same result
- `Consistency` (4): circuit invariants
- `LinearSystem` (3): Gauss-Jordan basics
- `Solver` (12): KCL, Ohm's law, potential distribution, power
- `Circuit` (60): add/remove nodes and components, graph operations

## Key design decisions

1. **`double` for physics quantities, `float` for graphics** — all world-space coordinates and potentials use double; only final pixel positions use float.
2. **Wire thickness in world units × camera.scale** — wire is NOT a fixed pixel width; it's a physical conductor that grows with zoom. At scale=1 an 8wu wire is 8px wide; at scale=50 it's 400px.
3. **Potential gradient fills full cross-section** — like a CFD/heat simulation color map, not a thin strip. This enables fluid-sim-style visualization at any zoom.
4. **Surface charge model**: σ ∝ (V − V_avg_wire) / V_swing, NOT pure d²V/dx² (which is 0 for uniform wires). Laplacian term added as a weak junction detector. Dots on BOTH edges (top and bottom).
5. **Particle count ∝ wire volume**: N = max(12, min(1200, volume/40)). Replaced old pixel-density-based count for physical consistency.
6. **Thermal motion**: 3-frequency sine/cosine combos deterministically seeded by particle index — looks like Brownian motion but is deterministic per particle.
7. **Gradient threshold**: `screenW > 1.0f` (was 12.0f) to show potential at all zoom levels.
8. **E-field spread at high zoom**: ≤5 rows of arrows across wire cross-section when screenW > 24px.
9. **No filled circles at wire endpoints** (see bug fix below).

## Bugs fixed this session
1. **Dark endpoint circles as "cut-outs"**: `AddCircleFilled` at wire ends extended past the rectangular gradient quads, creating visible dark semicircles that looked like dead zones. **Fix**: removed all 6 `AddCircleFilled` calls, kept only `AddCircle` outlines for rounding.
2. **`int a` shadowing `Vec2 a`**: in `drawWire` for-loop, `int a` in color extraction `(pc >> 24) & 0xFF` shadowed the function parameter `Vec2 a`. **Fix**: renamed to `int aa`.
3. **Laplacian calculation**: `dtInv * segmentLen` gave wrong neighbor distances in `drawSurfaceCharge`. **Fix**: replaced with correct `segmentLen` computation.
4. **Potential gradient barely visible**: `screenW < 12.0f` threshold filtered everything at default zoom. **Fix**: lowered to `screenW > 1.0f`.

## UI layout

```
┌──────────┬──────────────────────────────────────┬──────────┐
│ Toolbar  │                                      │Inspector │
│  Mode    │          Canvas (circuit)            │  Panel   │
│  Buttons │                                      │          │
│  Toggles │                                      │          │
│  Slider  │                                      │          │
├──────────┴──────────────────────────────────────┴──────────┤
│                       Log Panel                             │
└─────────────────────────────────────────────────────────────┘
```

- Left: 160px (resizable) — toolbar with mode selector, solver buttons, visualization checkboxes, Wire Width slider (2–50 wu), Distrib.Wire checkbox
- Center: flexible — canvas
- Right: 280px (resizable) — inspector (component properties)
- Bottom: 120px — log

## Known limitations / caveats

1. **README.md is outdated** — describes only Milestone 1 features, doesn't mention any of the physics visualization work (potential gradient, E-field, drift, surface charge, etc.)
2. **No circuit save/load** — no serialization of any kind
3. **Only one ground node** — MNA solver expects exactly one
4. **No capacitor/inductor** — DC steady-state only, no transient simulation
5. **Surface charge model is heuristic** — V-based sigma is a visual approximation, not a physical surface charge calculation from Maxwell's equations
6. **Particle thermal motion is deterministic** — uses sine/cosine combos, not actual random noise. Looks good but particles always follow the same path for the same configuration.
7. **Magnetic field is decorative** — ring count/spacing is proportional to current, not computed from Biot-Savart law
8. **No energy conservation tracking** — only shows instantaneous power
9. **Resistor body width in screen pixels**: `(wireW * 2.8) / m_camera.scale` — this formula couples resistor appearance to wire thickness but is a bit unphysical.
10. **`distributedWire` mode is partially broken** — the `m_readOnly` flag freezes editing when checked, but the distributed circuit rendering may have visual artifacts.

## Build system notes

- **CMake FetchContent** for ALL non-system dependencies (GLFW, GLM, ImGui, GTest)
- **Separate test executable** (`current-lab-tests`) — does NOT link GLFW/OpenGL, only links circuit+solver+math sources and GTest. ImGui headers are included (for IM_COL32 etc.) but imgui.cpp is NOT compiled into tests.
- **GLM is fetched but barely used** — the project has its own `Vec2.h` which serves all 2D math needs.
- **C++20** required, GCC 14 on Ubuntu 24.04.

## Potential next tasks

### High priority
1. **Fix magnetic field toggle feedback** — the ring rendering doesn't look right at all zoom levels
2. **Update README.md** to reflect current feature state
3. **Add circuit save/load** (JSON serialization - Circuit + Solution)
4. **Add undo/redo** for editing operations

### Medium priority
5. **Replace surface charge heuristic with physical model** — compute σ from ∇·E at wire surface (requires solving Laplace's equation or using charge relaxation method)
6. **Add capacitor and transient RC simulation** (requires time-stepping solver)
7. **Energy conservation visualization** — Sankey diagram or flow-based
8. **Replace deterministic thermal motion with seeded PRNG** for more realistic Brownian motion
9. **Add proper Biot-Savart magnetic field computation** for accurate ring visualization
10. **Add inductor** — requires mutual inductance matrix in MNA

### Low priority
11. **Water analogy overlay** (educational mode)
12. **Hole visualization** (educational semiconductor model)
13. **Multiple reference nodes** — demonstrate relativity of voltage
14. **Port to Wayland native** (currently XWayland via DISPLAY=:0)

## Questions for the next agent

1. **Distributed wire model**: the `toDistributed(N)` function splits a wire into N resistor segments. When `m_distributedWire` is checked, it switches rendering to the distributed circuit. But `m_readOnly` is set to true — should editing work on distributed circuits? Currently the solver uses either `m_circuit` or `m_distributedCircuit` but the canvas receives only one at a time. Is this the intended behavior or should distributed wire always be the underlying model?

2. **Resistor body width**: `rectH = (wireW * 2.8) / m_camera.scale` — this makes resistor body width scale inversely with zoom (wider at low zoom, narrower at high zoom). Was this intentional (to keep the resistor visible at overview) or should it be a fixed world-unit size like the wire?

3. **Particle trail mode**: drift particles currently jump every frame. Should particles be drawn with a trail (last N positions) for a more fluid-like visualization? This requires storing per-particle history (which we don't currently have).

4. **Gradient strip optimization**: at high zoom with large wires (e.g., thickness=50, scale=50 → screenW=2500px), the N=1000 gradient strips are rendered every frame. Should we consider caching the gradient to a texture or ImGui image? Performance seems OK currently but could degrade.

5. **Test coverage for physics rendering**: the current tests cover canvas state and geometry but don't test actual rendering output (pixel colors, particle positions). Should we add image-diff tests (compare screenshots with golden images)? This would require a headless GL context or a software rasterizer.
