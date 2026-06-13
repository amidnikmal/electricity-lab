# Mechanics Projection

Date: 2026-06-10

A third projection of the same `CircuitModel`: the circuit rendered as a mechanical chain machine (inspired by the Mechanics construction-set idea, own styling, no asset copying). Electrical quantities become visible motion. Built by `ProjectionBuilder` (`ProjectionKind::Mechanical`) from exactly the same model + solver solution as the Schematic and Physics projections — one `ComponentId`, N projections.

## Analogy table (implemented in `MechanicsMapping.h`)

| Electrical | Mechanical analog | Visual |
| --- | --- | --- |
| Current I | chain linear speed (sign = direction) | moving chain links |
| Potential V | chain tension / "height" vs anchor | tension colour (same palette as Potential layer) |
| Resistor R | friction brake | pads clamping the chain + heat glow |
| Voltage source | drive sprocket | toothed source gear with chain rollers seated on the pitch circle |
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

## Source drive sprocket

The voltage source is not rendered as a decorative crank beside the chain. Its
mechanical body is the drive sprocket itself: `ChainSim` routes the source loop
over a central pitch circle, and the renderer draws the same pitch circle as a
toothed gear under the rollers. The no-slip visual contract is:

- source gear pitch radius comes from `chain_geometry::driveSprocketPitchRadius`,
- source rollers sit on that pitch radius,
- source teeth use the same chain pitch (`one roller per tooth gap`),
- the drive pitch radius is only slightly larger than the straight chain run
  offset, so the chain has limited contact patches and stays visually taut,
- positive chain travel spins the source gear with phase `-travel / pitchR`
  (the tooth surface moves with the chain, not against it),
- the mouse grab handle is on the drive sprocket rim and still maps angular
  speed to temporary EMF through `emfFromCrankSpeed`.

## Applicability limits

- The analogy is one-to-one for lumped DC/transient circuits. It does not extend to field-level effects (surface charge, magnetic field geometry) — those layers stay in the Physics projection.
- The flywheel's *displayed* spoke angle is an animation phase, not the integral of I dt; only its rate and direction are mapped.

## UI

Projection selector: `Single` layout + projection combo (Circuit | Physics | Mechanics), or `Dual` (Circuit + Physics), or `Triple` (Circuit + Physics + Mechanics). Selection and camera sync work across all visible panes through the shared `DualViewState` (one `ComponentId` highlighted everywhere; editing always mutates the single `CircuitModel`).

## Tests (tests/test_mechanics.cpp)

Mapping monotonicity and sign-correctness, reversal with current sign, exact power correspondence, energy identities, element parity with other projections, chain phase reversal, spring contraction on charging, triple-pane camera sync and layout split.
