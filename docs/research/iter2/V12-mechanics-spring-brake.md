# V12: Mechanics — Spring & Brake Verdict (iter2)

Date: 2026-06-14
Re-evaluates: A8 (DISCREPANCY 1, PHYSICS-BUG 3)
Source: impedance analogy (Maxwell, 1873)

## Verdict

| # | A8 claim | Verdict | Evidence |
|---|----------|---------|----------|
| 1 | `springCompressionFromVoltage` returns Vc, not q=C·Vc | **CONFIRMED** | `src/projection/MechanicsMapping.h:46-48` — signature `(double capVoltage)`, body `return capVoltage;`. No capacitance parameter. `ProjectionBuilder.cpp:1191-1193` normalizes `|vc|/vRange` directly. Two caps with C=1µF and C=1000µF at same Vc → identical spring compression. Headline comment on line 45 acknowledges `q = C*Vc <-> compression` but code ignores C. |
| 2 | Brake scales speed by constant 0.82, not ∝ current | **CONFIRMED** | `src/physics/ChainSim.cpp:104-105` — `speed *= 0.82` per frame, no `dt` scaling, no resistance parameter. All brakes identical regardless of R value. In the impedance analogy, damper force F = R_m·u ↔ V = R·I, implying `dv/dt = -(R_m/M)·v` → `speed -= (R/M)*speed*dt`. The 0.82 is a frame-dependent visual gesture, not a physical viscous damper. |

## Root cause

Both elements use **unit-scale visual defaults** (k=1) instead of component values:
- Spring ignores capacitance → `x = Vc` instead of `x = C·Vc`
- Brake ignores resistance → `speed *= 0.82` instead of `speed *= (1 - R·dt/M)`

## Source

- Wikipedia «Impedance analogy» — capacitor ↔ spring compliance, resistor ↔ damper
- `src/projection/MechanicsMapping.h:46-48` — springCompressionFromVoltage
- `src/projection/ProjectionBuilder.cpp:1191-1193` — emitSpring displacement
- `src/physics/ChainSim.cpp:104-105` — brake advance logic
