# Physics Visual Layer Status

Date: 2026-06-10

This document separates solved physics from educational visualization. Visual layers must not imply more accuracy than the current model provides.

Status legend: **exact** = direct image of solver values; **approx** = physically grounded within the stated model; **heuristic** = sign/shape plausible, magnitudes not solved; **visualization** = pedagogical animation only.

| Layer | Model | Status | Default visibility | Risk |
| --- | --- | --- | --- | --- |
| Potential | circuit/distributed wire voltage interpolation | approx (valid in chosen 1D model) | on in Potential mode | medium |
| E-field (along conductor) | -dV/dx along wire; resistor-body and capacitor-gap lengths used where relevant | approx 1D | on in Field mode | medium |
| E-field backdrop (streamlines) | qualitative point-source field from node potentials | heuristic | on with Field layer | high |
| Heat | dissipated power only, P = I*dV for R/wire | exact-sign / approx intensity | on in Power mode | low |
| Surface charge | sigma ~ (V - Vavg) heuristic with junction booster | heuristic | off by default | high |
| Magnetic field | B ~ I/r magnitude, right-hand-rule polarity, local 2D glyphs | qualitative | off by default | high |
| Drift particles | solver current direction/sign; amplified speed + thermal jitter | visualization (sign exact) | off by default | medium |
| Capacitor plate charge / gap field | charge dots and gap arrows scale with solved Vc; E = Vc / gap | approx (gap geometry is symbolic) | on with Charges/Field layers | medium |
| Capacitor energy glow | intensity from solved Vc, energy = 1/2 C V^2 | exact energy, heuristic radius | on with Field layer | low |
| Inductor magnetic-energy glow | intensity from solved Il, energy = 1/2 L I^2 | exact energy, heuristic radius | on with Magnetic layer | low |
| Transient values (Vc, Il, t) | companion-model MNA (backward Euler / trapezoidal), see REALTIME_TRANSIENT_MODEL.md | exact within integration error (stated dt) | Transient mode | low |
| Mechanics chain speed/direction | speed = k*I, sign exact; on-screen link motion amplified | exact mapping, visualization speed | Mechanics projection | low |
| Mechanics tension colour | same node potentials as Potential layer | approx (same model) | Mechanics projection | medium |
| Mechanics spring compression | displacement proportional to solved Vc | exact mapping, symbolic geometry | Mechanics projection | low |
| Mechanics flywheel spin | angular momentum proportional to solved Il; spoke phase animated | exact mapping, visualization speed | Mechanics projection | low |
| Mechanics brake heat | dissipated power only (same as Heat) | exact-sign | Mechanics projection | low |

## Notes

- Solver branch values remain the source of truth for current, voltage drop, and signed power. All projections (Schematic, Physics, Mechanics) are built by `ProjectionBuilder` from the same model + solution; no projection invents physics.
- Heat uses dissipated power only. Sources that supply power are not heat sources in this layer.
- Surface charge is intentionally isolated to Charges and Debug presets until a stronger model is implemented.
- Magnetic visualization is not a calibrated field solver. It should remain hidden in normal learner presets.
- Drift particles and mechanics chain/flywheel motion are visual timing aids: directions and relative magnitudes are solver-honest, animation speed is amplified for visibility (`kVisual*` constants in `ProjectionBuilder.cpp`).
- "Simulation realism" (transient time-stepping) and "field realism" (1D-along-wire approximations) are different things; the transient solver makes the circuit *values* honest over time, it does not upgrade the field layers.
- The mechanics projection preserves power correspondence exactly: tension x speed = V x I (unit constants multiply to 1, see `MechanicsMapping.h`).
