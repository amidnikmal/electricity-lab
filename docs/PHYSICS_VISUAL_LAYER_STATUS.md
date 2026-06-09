# Physics Visual Layer Status

Date: 2026-06-09

This document separates solved physics from educational visualization. Visual layers must not imply more accuracy than the current model provides.

| Layer | Model | Status | Default visibility | Risk |
| --- | --- | --- | --- | --- |
| Potential | circuit/distributed wire voltage interpolation | approximate/valid in chosen model | on in Potential mode | medium |
| E-field | -dV/dx along wire | approximate 1D | on in Field mode | medium |
| Heat | P = I*dV / local dissipation | reasonable | on in Power mode | low |
| Surface charge | heuristic unless replaced | conceptual | off by default | high |
| Magnetic field | qualitative or B proportional to I/r if implemented | qualitative | off by default | high |
| Particles | educational drift visualization | conceptual | off by default | medium |

## Notes

- Solver branch values remain the source of truth for current, voltage drop, and signed power.
- Heat should use dissipated power only. Sources that supply power are not heat sources in this layer.
- Surface charge is intentionally isolated to Charges and Debug presets until a stronger model is implemented.
- Magnetic visualization is not a calibrated field solver. It should remain hidden in normal learner presets.
- Drift particles are visual timing aids. They should not imply that electrons transport energy from source to load at the rendered animation speed.
