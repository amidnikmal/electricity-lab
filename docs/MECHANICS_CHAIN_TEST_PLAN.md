# Mechanics Chain Endurance Test Plan

The mechanics view must not only draw chain links; the simulated chain has to
keep moving under solver current. The failure to catch is a chain that looks
present but never advances in any demo.

## Target Circuit

Use a single-load closed loop:

- one voltage source,
- one resistor,
- one return wire,
- one ground node.

The resistor is the only load. The source exists only to produce nonzero
current; a resistor alone cannot move a chain because the electrical model has
no current source.

## Main Contract

Pick one marked chain link on the resistor loop and require it to complete at
least ten full laps around its closed oval path without stalling.

The test should fail if:

- the chain has zero target speed despite nonzero current,
- the drive is only rendered but not applied to `ChainSim`,
- the resistor brake acts as a hard stop,
- the link leaves the guide or produces non-finite coordinates,
- motion happens in one burst and then stalls.

## Measurement

The test needs a phase tracker for one link:

```text
phase = closest station on oval / oval perimeter
unwrapped_laps += unwrap(phase_delta)
```

For the current `ChainSim`, `ChainLink::indexInLoop` is only the body index,
not the current station. The test should compute phase from the link position
and the same oval geometry rules used by the simulator.

## Required Tests

### 1. Marked Link Completes Ten Laps

Build the single-load loop, derive `ChainSpec` from the solved branch current,
configure `ChainSim`, mark one link, and step with fixed `dt = 1/60`.

Expectation:

- the marked link reaches `>= 10` signed laps within a finite simulation time,
- every sampled position is finite,
- most one-second windows show positive progress.

### 2. Resistor Brake Does Not Stop The Chain

Run the same test with `brake = true` on the resistor loop.

Expectation:

- the marked resistor link still reaches ten laps,
- progress remains continuous enough to reject long stalls.

### 3. Direction Follows Current Sign

Reverse the source polarity and run a shorter phase test.

Expectation:

- the signed lap direction reverses.

### 4. Chain Geometry Remains Bounded

During the ten-lap run, check all links:

- coordinates stay finite,
- no link drifts far outside the oval guide envelope.

## Handoff Order

1. Commit this test plan.
2. Add phase-tracking helpers and a small calibration test.
3. Add the ten-lap endurance test.
4. If it fails, fix `ChainSim` with the failing test kept enabled.
5. Run the targeted mechanics tests and then the full test suite.
