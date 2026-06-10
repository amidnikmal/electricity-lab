# Hydraulic Flow Test Plan

This plan defines how the water analogy should be tested against the electrical
model. The goal is not to make Box2D water numerically identical to electron
transport. The goal is to preserve the invariants that make the two pictures
explain the same circuit behavior.

## Source Of Truth

- The circuit solver owns electrical truth: branch current `I`, voltage drop
  `dV`, and power `P = I * dV`.
- Hydraulic particles are coarse visual carriers. They must preserve flow
  direction, monotonic flow magnitude, continuity through a series path, and
  resistor dissipation cues.
- Visual speed is amplified, so tests compare normalized rates and ratios, not
  SI units.

## Measured Quantity

The main measured quantity is signed visual volume flow through a virtual
cross-section of a pipe:

```text
Q_vis = signed_crossings * particle_area / elapsed_time
particle_area = pi * particle_radius^2
```

A crossing is counted when the same particle moves from one side of a station
`t = constant` to the other side along the component axis. Positive crossings
follow `nodeA -> nodeB`; negative crossings oppose it.

This is deliberately different from mean velocity. Mean velocity can look right
while particles pile up before a resistor. Cross-section transport catches
actual throughput.

## Required Tests

### 1. Flow Rate Scales With Current

Build two otherwise identical loops with different resistor values. After
spin-up, measure `Q_vis` through the same wire section.

Expectation:

- The sign of `Q_vis` matches branch current.
- The higher-current loop has higher `abs(Q_vis)`.
- The flow ratio is within a broad statistical band around the solver current
  ratio. Granular contacts and visual clamps make exact proportionality
  inappropriate.

### 2. Series Flow Is Conserved Through The Resistor

In a single source-resistor-wire loop, measure `Q_vis` at:

- a pipe section before the resistor,
- the resistor body,
- a pipe section after the resistor.

Expectation:

- Signs match each branch current.
- Magnitudes are comparable inside one time window.
- No section is near zero while the others are moving.

This is the water analogue of the same current through every element in a
series circuit.

### 3. Resistor Does Not Accumulate Water

Track particle count inside the resistor body over several equal time windows.

Expectation:

- Count has no sustained upward or downward trend.
- Final count remains close to the time-window average.

This catches "particles enter the resistor but do not leave" and "the resistor
acts like a hidden sink/source".

### 4. Flow Through Resistor Is Temporally Uniform

Measure per-window crossing counts through the resistor body.

Expectation:

- Most windows have nonzero transport.
- Coefficient of variation is bounded by a loose granular-flow threshold.

The visual should read as continuous current, not occasional bursts separated
by long stalls.

### 5. Hydraulic Power Cue Tracks Electrical Power

For resistors with different `I` and `dV`, compare the solver power with the
hydraulic projection cues.

Expectation:

- Larger dissipated electrical power gives a stronger resistor heat/constriction
  cue.
- This is tested via projection primitives or the existing power/heat model,
  not via particle kinetic energy.

## Statistical Policy

- Always include a spin-up period before measuring.
- Measure over many fixed windows instead of one frame.
- Prefer ratio/order assertions over exact equality.
- Keep tolerances loose enough for deterministic Box2D contact noise, but tight
  enough to catch frozen flow, one-way accumulation, and broken current scaling.

## Implementation Order

1. Add test-only flow measurement helpers.
2. Add conservation and accumulation tests around the resistor.
3. Add current-scaling tests.
4. Add temporal-uniformity tests.
5. Add projection-level power cue tests if existing projection tests do not
   already cover the same contract.
