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
| Capacitor C | spring | movable plate, displacement proportional to Vc |
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

## Applicability limits

- The analogy is one-to-one for lumped DC/transient circuits. It does not extend to field-level effects (surface charge, magnetic field geometry) — those layers stay in the Physics projection.
- The flywheel's *displayed* spoke angle is an animation phase, not the integral of I dt; only its rate and direction are mapped.

## UI

Projection selector: `Single` layout + projection combo (Circuit | Physics | Mechanics), or `Dual` (Circuit + Physics), or `Triple` (Circuit + Physics + Mechanics). Selection and camera sync work across all visible panes through the shared `DualViewState` (one `ComponentId` highlighted everywhere; editing always mutates the single `CircuitModel`).

## Tests (tests/test_mechanics.cpp)

Mapping monotonicity and sign-correctness, reversal with current sign, exact power correspondence, energy identities, element parity with other projections, chain phase reversal, spring contraction on charging, triple-pane camera sync and layout split.
