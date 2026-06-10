# Element Library

Date: 2026-06-10

Every element is one `Component` in the single `CircuitModel` (one `ComponentId` across all projections) with: solver behavior (DC + transient), schematic symbol, physics projection, spintronics projection, inspector/editor support, and pure-logic tests.

`Component.value` semantics per type: R = ohms, V source = volts, C = farads, L = henries, Diode = unused (ideal), Switch = 1 closed / 0 open.

| Element | DC model | Transient model | Schematic symbol | Physics projection | Spintronics projection | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Wire | 1e9 S (or distributed R chain) | same | rounded conductor | potential gradient, drift, E-arrows, surface charge | moving chain | done |
| Resistor | G = 1/R | same | body + leads (sectioned) | body-concentrated field, heat underline, drift slowdown | friction brake pads + heat glow | done |
| Voltage source | MNA branch row | same | circle with +/- | leads carry gradient | drive crank (spokes pump with I) | done |
| Ground | reference node | same | ground bars | same | anchor block | done |
| Capacitor | open (gmin 1e-12 S) | BE: g=C/dt, ieq=g*Vc; TR: g=2C/dt, ieq=g*Vc+Ic | two plates + gap | plate charges, gap E-field, energy glow (1/2 C V^2) | spring, displacement ~ Vc | done |
| Inductor | short (1e9 S) | BE: g=dt/L, ieq=-Il; TR: g=dt/2L, ieq=-(Il+g*Vl) | 4-bump coil | magnetic rings, energy glow (1/2 L I^2) | flywheel, momentum ~ Il | done |
| Diode | ideal PWL: conducting=1e9 S, blocking=1e-12 S, state iteration (max 24 passes) | same companions inside each step | triangle + bar | generic conductor layers (current only when forward) | ratchet pawl | done (ideal only) |
| Switch | closed=1e9 S, open=1e-12 S | same | lever + contacts, open/closed label | generic conductor layers | coupler (gap when open) | done |

## Diode details

Ideal piecewise-linear model: conducts A -> B at zero forward drop when forward biased, blocks otherwise. Solved by fixed-point iteration over diode states around the linear MNA solve (`CircuitSolver::solveIterative`): start all blocked; a conducting diode carrying negative current flips to blocked, a blocked diode with positive bias flips to conducting; repeat until consistent (<= 24 passes). Works identically in DC and inside every transient step (peak-detector test holds capacitor charge when the source drops).

The exponential Shockley model (I = Is(e^{V/nVt}-1)) is **not** implemented; it needs Newton iterations with conductance linearization and is left as a flagged future option.

## Switch details

Topology stays fixed; open/closed is a conductance swap (1e-12 / 1e9 S), so transient state (Vc, Il) survives toggling — opening a switch mid-charge freezes the capacitor voltage (covered by test).

## Defaults (placement)

R = 1 kOhm, V = 5 V, C = 1 mF (tau = 1 s with 1 kOhm), L = 1 H (tau = 0.1 s with 10 Ohm), switch = closed.

## Tests

`tests/test_solver.cpp`, `test_transient.cpp` (RC/RL/energy/balance/stability/convergence), `test_elements.cpp` (C/L geometry, symbols, stored energy, placement), `test_diode_switch.cpp` (forward/reverse, peak detector, open/closed, mid-transient freeze, symbols in all projections), `test_spintronics.cpp` (analog mapping).
