# Physics Audit

## Layer Table

| Layer | Current implementation | Physical status | Risk | Action |
|---|---|---|---|---|
| Potential | Node voltages interpolated along conductors in distributed 1D wire mode | reasonable in 1D distributed approximation | medium | keep, document reference dependence |
| E-field | `E ~= -dV/dx` sample arrows from pure `FieldModel` | reasonable in 1D approximation | medium | keep extracted model, not a 3D solve |
| Surface charge | `sigma ~ (V - Vavg)` with junction-strength boost | heuristic | high | explicitly label heuristic, keep replaceable API |
| Magnetic field | `B ~ I/r` page-normal glyphs from `MagneticFieldModel` | qualitative quasi-static | high | keep as qualitative teaching layer |
| Drift | deterministic particles from `DriftModel`, thermal motion qualitative, speed amplified | educational visualization | medium | keep explicit speed amplification label |
| Heat | dissipated-power glow only | reasonable | low/medium | keep sign-cleaned power model |
| Power | `P = I * dV` per branch | exact sign convention within circuit model | low | keep |

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

## Explicit Non-Goals

- No full Maxwell solution
- No 3D field geometry around arbitrary conductors
- No transient charge relaxation
- No temperature-dependent resistance
