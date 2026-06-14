# Realtime Transient Model

Date: 2026-06-10

## Scope

The transient mode shows the circuit's *process in time*: capacitor charging, inductor current rise, switch/diode transients. It complements (does not replace) the DC steady-state mode. Both modes share one `CircuitModel` and one MNA assembly (`CircuitSolver::solveWithCompanions`).

## Method

Time-domain simulation by **companion models** inserted into the MNA matrix at each step.

Every reactive element is replaced by a conductance `g` in parallel with a history current source `ieq`, with the universal branch model:

```text
i_ab = g * (Va - Vb) - ieq      (current flowing from nodeA to nodeB)
```

### Backward Euler (default; 1st order, A-stable)

| Element | g | ieq |
| --- | --- | --- |
| Capacitor C | C / dt | (C/dt) * Vc(t) |
| Inductor L | dt / L | -Il(t) |

### Trapezoidal (optional; 2nd order, more accurate at the same dt)

| Element | g | ieq |
| --- | --- | --- |
| Capacitor C | 2C / dt | (2C/dt) * Vc(t) + Ic(t) |
| Inductor L | dt / 2L | -(Il(t) + (dt/2L) * Vl(t)) |

A component's **first** step always uses backward Euler ("self-starting" scheme): the trapezoidal history current/voltage is undefined at t = 0, and an inconsistent start injects a persistent ringing artifact. The first BE solve makes the history consistent; trapezoidal takes over from step 2.

### Per-step algorithm

1. Build companions for all C/L from the stored state (`TransientState`: `capVoltage`, `indCurrent`, plus trapezoidal histories).
2. Assemble MNA (resistors, wires, switches stamp conductances; voltage sources add MNA rows; companions stamp `g` and inject `ieq`).
3. If the circuit has diodes, iterate the ideal-diode states (see ELEMENT_LIBRARY.md) around the linear solve.
4. Solve; update `Vc`, `Il` (and histories) from the branch results; advance `state.time += dt`.

## DC steady-state treatment

- Capacitor: open circuit (gmin leak of 1e-12 S to keep the matrix non-singular).
- Inductor: short circuit (1e9 S, same constant as ideal wires).

## Initial state snapshot (`solveTransientSnapshot`)

The honest t = 0+ picture without advancing time: capacitors are held at their stored Vc (stiff source, 1e9 S), inductors at their stored Il (current source with gmin). Used after Reset and whenever the circuit is edited in transient mode.

## dt, stability, applicability

- dt = simSpeed / solveHz, где solveHz — целевая частота решений MNA (default 60 Гц реального времени), simSpeed — sim-секунд на реальную секунду (авто-подбор по tau цепи или ручной override). frame cap = maxStepsPerFrame = 8.
- Backward Euler is A-stable: at any dt the RC/RL response stays bounded and monotonic (validated by test `TransientStability.BackwardEulerDoesNotBlowUpAtHugeDt` with dt = 5 tau). Large dt costs accuracy, never stability.
- Trapezoidal halves nothing magical: at dt = tau/20 its error at t = tau is ~2 orders of magnitude below BE (test `TrapezoidalBeatsBackwardEulerAtSameDt`), but it can ring on stiff inputs; BE remains the default.
- Convergence: error at t = tau decreases monotonically over dt = 0.1/0.01/0.001 tau (test `TransientConvergence.ErrorShrinksAsDtShrinks`).
- "Sim speed" in the UI maps real seconds to simulated seconds (steps per frame are capped at 2000; the simulation lags rather than hitching).

## What is exact / what is approximate

Exact (within linear-solver precision):
- Tellegen power balance at every solved time point: sum of all branch powers = 0 (test `PowerBalancesEveryStep`).
- Companion algebra: branch currents/voltages are the discretized ODE solutions.

Approximate:
- Time discretization error: O(dt) for BE, O(dt^2) for trapezoidal.
- Accumulated stored energy matches 1/2 C V^2 and 1/2 L I^2 to ~3% at dt = tau/1000 (tests `StoredCapacitorEnergyMatchesHalfCV2`, `StoredInductorEnergyMatchesHalfLI2`).

Not modeled: AC sources, nonlinear capacitors/inductors, mutual inductance, distributed reactances.

## Validated cases (tests/test_transient.cpp)

- RC charge: Vc(tau) = 0.632 V_src, Vc(10 tau) -> V_src.
- RC discharge: Vc(tau) = 0.368 Vc(0) with the same tau.
- RL rise: I(tau) = 0.632 V/R, I(infinity) -> V/R.
- Energy bookkeeping and Tellegen balance, stability, convergence, snapshot honesty.

Separation of concerns: drift-particle and chain animation speeds remain *visualization* (amplified); transient time is the real simulated time of the circuit state.
