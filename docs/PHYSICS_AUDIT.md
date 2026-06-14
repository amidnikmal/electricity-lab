# Physics Audit

## Layer Table

| Layer | Current implementation | Physical status | Risk | Action |
|---|---|---|---|---|
| Potential | Node voltages interpolated along conductors in distributed 1D wire mode | reasonable in 1D distributed approximation | medium | keep, document reference dependence |
| E-field | `E ~= -dV/dx` sample arrows from pure `FieldModel` | reasonable in 1D approximation | medium | keep extracted model, not a 3D solve |
| Surface charge | `sigma ~ (V - Vavg)` with junction-strength boost | heuristic | high | explicitly label heuristic, keep replaceable API |
| Magnetic field | `B ~ I/r` page-normal glyphs from `MagneticFieldModel` | qualitative quasi-static | high | keep as qualitative teaching layer |
| Drift | deterministic particles from `DriftModel`, thermal motion qualitative, speed amplified | educational visualization | medium | keep explicit speed amplification label |
| Heat | dissipated-power glow (instantaneous) + lumped RC temperature readout | reasonable, display-only | low/medium | keep sign-cleaned power model; temperature is a one-way display |
| Power | `P = I * dV` per branch | exact sign convention within circuit model | low | keep |
| Temperature | lumped RC per branch: `C_th dT/dt = P_diss - (T - T_amb)/R_th`, backward Euler | display-only thermal model, no feedback | low | keep one-way; never feed back into resistance |

## Solver Audit

- MNA branch signs remain:
  - current positive from `nodeA -> nodeB`
  - voltage drop `dV = Va - Vb`
  - power positive for dissipation, negative for supplied power
- Ground is now resolved by node ID, not vector index
- Non-contiguous node IDs are supported
- Zero-ohm resistors are treated numerically as near-wire conductance

## Distributed Wire Audit

- Wire resistance is now parameterized by:
  - `segmentsPerWire`
  - `resistancePerUnit`
- Original node/component identity is preserved across distribution mapping
- Linear voltage drop along uniform wire remains the intended model

## Thermal Model Audit

- Lumped (sosredotochennaya) RC thermal model, **display-only**: `physics/ThermalModel.h`.
  - State `ThermalState`: `temperature[componentId]` in Kelvin, mirror of `TransientState` (has `reset()`).
  - Step: `C_th dT/dt = P_diss - (T - T_amb)/R_th`, backward Euler on the SAME `dt` as the electrical transient.
  - `P_diss = dissipatedPowerOnly(type, branch.power)` (nonzero only for Resistor/Wire) — reuses the existing power model.
  - Integrated over the DISTRIBUTED solution, so each wire segment is its own thermal node and the along-wire gradient emerges by itself; the per-element readout reports the hottest segment.
  - Constants in `PhysicalUnits.h`: `kAmbientTemperature = 293.15 K`, `kThermalCapacitance = 1.0 J/K`, `kThermalResistance = 50.0 K/W`.
  - Steady-state fixed point `T = T_amb + P_diss * R_th` (covered by `tests/test_thermal.cpp`).
- **No R(T) feedback**: temperature never re-enters the MNA. The solver and `LiveSim` core are untouched.

## Explicit Non-Goals

- No full Maxwell solution
- No 3D field geometry around arbitrary conductors
- No transient charge relaxation
- No temperature-dependent resistance — temperature is a one-way display fed by dissipated power; it is never fed back into resistance (no R(T))
