# Mechanics Projection

Date: 2026-06-10

A third projection of the same `CircuitModel`: the circuit rendered as a mechanical chain machine (inspired by the Mechanics construction-set idea, own styling, no asset copying). Electrical quantities become visible motion. Built by `ProjectionBuilder` (`ProjectionKind::Mechanical`) from exactly the same model + solver solution as the Schematic and Physics projections — one `ComponentId`, N projections.

## Analogy table (implemented in `MechanicsMapping.h`)

| Electrical | Mechanical analog | Visual |
| --- | --- | --- |
| Current I | chain linear speed (sign = direction) | moving chain links |
| Potential V | chain tension / "height" vs anchor | tension colour (same palette as Potential layer) |
| Resistor R | friction brake | pads clamping the chain + heat glow |
| Voltage source | drive sprocket | large gear with taut tangent chain, pump direction follows I |
| Capacitor C | spring between two crank arms on two counter-rotating shafts | spring deforms; relative shaft angle θ = charge, restoring moment = Vc |
| Inductor L | flywheel | spinning wheel, angular momentum proportional to Il |
| Diode | ratchet | pawl triangle, single allowed direction (A -> B) |
| Switch | coupler | chain gap (open) / clamp (closed) |
| Ground | fixed anchor | hatched anchor block |
| Junction node | idler pulley | pulley disc |

## Power correspondence (exact by construction)

`kTensionPerVolt * kLinkSpeedPerAmp = 1`, so mechanical power `tension * speed` equals electrical `P = V * I` identically (test `MechanicalPowerEqualsElectricalPower`). Brake heat uses dissipated power only, the same rule as the Physics heat layer.

Energy bookkeeping carries over unchanged:

- spring energy = 1/2 C Vc^2 (charge <-> compression),
- flywheel energy = 1/2 L Il^2 (current <-> angular momentum),

both identical to the electrical formulas (test `AnalogEnergiesMatchElectricalEnergies`).

## What is exact / what is metaphor

Exact (solver-honest):
- chain direction and relative speed (sign and magnitude of I),
- spring displacement proportional to Vc (visible charging in transient mode),
- flywheel spin direction and angular-momentum magnitude proportional to Il,
- brake heat proportional to dissipated power,
- tension colours = the same node potentials as every other view.

Metaphor / visualization:
- the on-screen animation rates (`kVisualChainSpeed`, `kVisualSpinRate`) are amplified for visibility, like drift particles;
- geometry (pulley sizes, pad shapes, spring teeth) is symbolic;
- "tension" maps potential, but a real chain cannot have negative tension — sign is carried by colour and direction, not by slack chain.

## Voltage source drive sprocket

The voltage source is modeled as a large drive sprocket on the component
midpoint. The chain is not made to look taut by shrinking that sprocket. Instead
`ChainGeometry::sourceDrivePath` builds a closed bicycle-chain path from common
external tangents between the endpoint idler sprockets and the central source
sprocket:

- two straight taut runs approach the source gear under an angle;
- only short pitch-circle arcs touch the source gear;
- each contacting link lies tangent to the pitch circle at its contact point;
- source sprocket phase is `-chainTravel / pitchRadius`, so the teeth move with
  positive chain travel rather than against it.

Tests in `tests/test_chain_gear.cpp` lock this down: large visual size is
preserved, tangent contact is enforced, the source contact arc stays short, and
rotation direction is checked from rendered tooth motion.

## Spring capacitor (crank-arm spring, one rigid body)

The capacitor is a **spring slung between two crank arms** on the two shaft
sprockets. Gear → arm → spring is **one rigid body**: a single shaft angle drives
the gear teeth, the arm and the spring together, so turning the gear deforms the
spring in lockstep (the spring is a rigid part of the drive, not a loose
decoration). The shaft angle is the loop travel: `shaftAngle =
clamp(chainTravel[cap]/R, ±~86°)`. In the linear region each cap gear turns at
exactly the loop SPEED — the two shafts are SEPARATE axles so they counter-rotate
(that is what lets the spring compress/stretch), but neither runs off-speed, so
nothing on one axle is out of sync.

- `emitSpring` (render only, from `mechanics::SpringCapacitorModel`): two shaft
  sprockets with teeth phased onto the crank-arm directions (teeth welded to
  arms); chain leads (`emitStaticChainOval`) rolling on `chainTravel[cap]`; the
  zigzag spring between the arm tips (coil step `= springLen/coils`, bunches when
  compressed) pinned pixel-exact to the charge-coloured knobs; mode + capacitance.
- Full participant in the rigid-axle coupling (`carriesChain` includes it) and
  MainWindow accumulates `chainTravel[cap]` at the SAME scale (`kMechChainBoost`)
  as everything, so in a series branch its current = the neighbour current and the
  whole thing turns as one. `resetMechanicsPhases()` zeroes the travel on discharge.

The clamp sits just under the crank's 90° fold: at the limit the spring is FULLY
compressed (max charge), it does not stick early (the earlier ±60° clamp bit
almost at once and looked stuck). At a parallel junction the only real limit is
that branches with different currents can't share one inextensible chain (needs a
differential — inherent to chain drives); a charged capacitor at DC steady simply
stops its own branch (i→0) while the rest of the loop runs.

Locked by `tests/test_mechanics_capacitor.cpp`:
- **`NoGearTurnsOutOfSyncWithTheLoop`** — every node/shaft sprocket turns at the
  same SPEED (counter-rotation allowed for separate shafts; off-speed fails);
- **`SpringCompressionAndGearTrackTravelTogether`** — +travel compresses & turns
  the gear one way, −travel stretches & reverses it (RLC synchrony);
- `SpringEndpointsPinnedToCrankKnobs`, `SpringCompressesAsCapacitorCharges`,
  `ChainRollsWithLoopTravelNotVoltage`, render-has-no-hidden-state, and the pure
  `SpringCapacitorModel` kinematics (sign invariant, even energy, coil spacing).

## Rigid-axle coupling (one spindle = one rotation)

Reference: in the Spintronics board game a node is a single physical spindle —
every sprocket and chain bolted to it turns together, one direction, no slip.
The earlier projection violated this: each component span its own oval and took
the chain direction from its *own* `nodeA -> nodeB` order. A leg wired backwards
relative to the current (negative branch current) then span its gear the opposite
way, so two chains on a shared node fought each other. `emitGears` only masked it
("mesh the dominant branch, independent sims can't co-phase").

`projection/MechanicsCoupling.h` (`computeAxleCoupling`) fixes the physics. In the
oval representation both end sprockets of a component necessarily turn the same
way for a given chain travel (an uncrossed belt over two pulleys), so a shared
node stays single-valued **only if every component in a connected mechanism
shares one rotation sign**. The sign is taken from the dominant drive (largest
`|I|`, voltage sources preferred); each chain keeps `|I|` as its speed magnitude.
Reversing the source reverses the whole machine.

- `MainWindow` is the source of truth: `targetSpeed = couplingSign · |mappedI|`,
  and `chainTravel` accumulates with that sign, so the simulated rollers
  (`chainLinks`) and the wheels (`chainTravel`) are coherent by construction.
- `ViewParams::coupling` carries the same signs into the stateless fallback so
  the no-sim animation (and tests) stay coherent too.
- At a true junction (3+ legs) the chains honestly carry different `|I|`; one
  rigid idler cannot be slip-free, so its spin *rate* follows the dominant leg
  while its *direction* is the shared axle sign. That is a physical limit of a
  shared axle, not a bug.

Locked down by `tests/test_mechanics_coupling.cpp`, including a control test that
reproduces the old opposite-direction behaviour from the raw per-component signs.

## Applicability limits

- The analogy is one-to-one for lumped DC/transient circuits. It does not extend to field-level effects (surface charge, magnetic field geometry) — those layers stay in the Physics projection.
- The flywheel's *displayed* spoke angle is an animation phase, not the integral of I dt; only its rate and direction are mapped.

## UI

Projection selector: `Single` layout + projection combo (Circuit | Physics | Mechanics), or `Dual` (Circuit + Physics), or `Triple` (Circuit + Physics + Mechanics). Selection and camera sync work across all visible panes through the shared `DualViewState` (one `ComponentId` highlighted everywhere; editing always mutates the single `CircuitModel`).

## Tests (tests/test_mechanics.cpp)

Mapping monotonicity and sign-correctness, reversal with current sign, exact power correspondence, energy identities, element parity with other projections, chain phase reversal, spring contraction on charging, triple-pane camera sync and layout split.
