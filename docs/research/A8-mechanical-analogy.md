# A8: Electro-Mechanical Analogy Research

Date: 2026-06-14
Agent: A8 (research)
Topic: Mechanical analogy (springs/flywheels/circuits) — impedance vs mobility, correctness of mappings and topology

## Summary of Findings

| # | Tag | Severity | Description |
|---|-----|----------|-------------|
| 1 | DISCREPANCY | Medium | Spring compression maps to Vc, not q=C·Vc (impedance analogy demands displacement ∝ charge) |
| 2 | DISCREPANCY | Low | `flywheelAngularMomentumFromCurrent` returns I, not L·I (J↔L implicit, but angular momentum = J·ω ↔ L·I) |
| 3 | PHYSICS-BUG | Low | Brake scales chain speed by constant 0.82, not proportional to current (damper force should be ∝ speed in impedance analogy) |
| 4 | IDEA | Info | Chain model: one loop per component, NOT one global loop — parallel branches have independent speeds, which is visual metaphor, not strict impedance-analog topology |
| 5 | IDEA | Info | Impedance vs mobility: code correctly uses impedance analogy (F↔V, u↔I) — self-consistent for sequence chain visualisation |
| 6 | IDEA | Info | Rotational impedance analogy: flywheel (J↔L, ω↔I) is correct; no explicit gyrator needed since both translational and rotational «flow» variables map to I |

## Detailed Analysis

### 1. Which analogy does the code use?

The code uses the **Impedance analogy** (Maxwell, 1873), also called force-voltage analogy:

| Domain | Effort variable | Flow variable |
|--------|----------------|---------------|
| Electrical | Voltage V | Current I |
| Mechanical (translational) | Force F / Tension | Velocity u / Chain speed |
| Mechanical (rotational) | Torque τ | Angular velocity ω |

Confirmed by:
- `MechanicsMapping.h:23-27`: `kTensionPerVolt = 1.0`, `kLinkSpeedPerAmp = 1.0`
- Power: `tension * speed = V * I` (test `MechanicalPowerEqualsElectricalPower`)
- `MECHANICS_PROJECTION.md:7-21`: explicit table: Current I → chain speed, Potential V → tension

### 2. Impedance analogy — canonical element mappings

Per Wikipedia «Impedance analogy» and «Mechanical–electrical analogies»:

| Electrical | Mechanical (impedance) | Constitutive |
|-----------|----------------------|--------------|
| Resistor R | Damper R_m | V = R·I ↔ F = R_m·u |
| Inductor L | Mass M | V = L·dI/dt ↔ F = M·du/dt |
| Capacitor C | Compliance C_m = 1/S | V = (1/C)∫I dt ↔ F = S·∫u dt = x/C_m |
| Voltage source | Constant force generator | — |
| Current source | Constant velocity generator | — |

Key: spring **stiffness** S ↔ **elastance** 1/C; spring **compliance** C_m ↔ **capacitance** C.
Spring displacement x = ∫u dt ↔ charge q = ∫I dt = C·V. Force F = S·x ↔ V = q/C.

**Topology flips**: series mechanical → parallel electrical; parallel mechanical → series electrical.

### 3. Rotational impedance analogy

Per Wikipedia:

| Electrical | Mechanical (rotational) |
|-----------|------------------------|
| Voltage V | Torque τ |
| Current I | Angular velocity ω |
| Inductance L | Moment of inertia J |
| Capacitance C | Rotational compliance |
| Resistance R | Rotational resistance |

τ = J·dω/dt ↔ V = L·dI/dt. Energy: ½ J·ω² ↔ ½ L·I².

### 4. DISCREPANCY 1 — Spring compression ∝ Vc, not q

- **Проект:** `src/projection/MechanicsMapping.h:46-48`, `src/projection/ProjectionBuilder.cpp:1191-1193`
- **Реальность/источники:** Wikipedia/Impedance analogy — spring displacement x = ∫u dt ↔ charge q = C·Vc, NOT voltage Vc. In the impedance analogy, capacitor voltage V = q/C corresponds to spring force F = S·x, and displacement x corresponds to charge q.
- **Идеал:** `springCompressionFromVoltage(capVoltage)` should return `capacitance * capVoltage` (i.e., q), scaling compression by capacitance value. Capacitors with larger C at the same Vc should show larger spring compression (more stored charge = more displacement).

The current code makes two capacitors with different C but same Vc look identical — missing the capacitance factor in the displacement. Energy `½ C V²` is correct electrically but the visual mapping to mechanical displacement misses the C factor: mechanical spring energy = ½ k·x² = ½(1/C)·q² = ½ C·V², so x = √(C/k)·V ≠ V (unless C = 1 unit).

### 5. DISCREPANCY 2 — flywheel angular momentum

- **Проект:** `src/projection/MechanicsMapping.h:51-53`
- **Реальность/источники:** Wikipedia/Impedance analogy — rotational: angular momentum = J·ω ↔ L·I. The function returns I, implicitly setting J = 1. This is internally consistent (energy identity holds), but it means the visual flywheel «heaviness» (moment of inertia) doesn't scale with inductance L — two inductors with different L at same I spin identically. The flywheel energy test (`AnalogEnergiesMatchElectricalEnergies`) passes by construction (both compute ½ L·I²), but the mechanical interpretation (½ J·ω²) only holds if J = L.
- **Идеал:** Either document the unit scaling explicitly (J = L by convention), or pass L into the function so visual spin rate/rim thickness reflects inductance magnitude.

### 6. PHYSICS-BUG — brake model

- **Проект:** `src/physics/ChainSim.cpp:104-105`
- **Реальность/источники:** In impedance analogy, damper force = R_m·u ↔ V = R·I. Brake force should be proportional to chain speed (current). The code multiplies speed by constant 0.82 (`speed *= 0.82`), which acts as a fixed friction factor independent of current magnitude.
- **Идеал:** Brake deceleration should be proportional to speed: `dv/dt = -(R_m/M)·v` or use `speed *= (1 - R_factor * dt)`. The 0.82 factor is a visual metaphor, not a physical damper analog. For a true impedance analogy, the brake should reduce speed in proportion to the speed (viscous damping), not by a constant factor.

### 7. IDEA — Topology: one loop per component vs one global loop

- **Проект:** `src/physics/ChainSim.h:8-13`, `src/physics/ChainSim.cpp:71-98`
- **Наблюдение:** Each component gets its own `Loop` with independent `targetSpeed`. In the strict impedance analogy, series-connected components would share a single chain loop (same current = same velocity = same chain speed). The code instead gives each component its own chain, each moving at its branch current. This is a valid visual metaphor but diverges from strict impedance-analog topology.
- **Контекст:** The chain loop is guided — link positions come from `Oval::at()` based on phase, no physics simulation. This is an intentional design choice for visual clarity, documented in `ChainSim.h:8-13`: «one series loop, one current».

### 8. IDEA — Mobility analogy alternative

The Mobility analogy (Firestone, 1933) would map:
- Force ↔ Current, Velocity ↔ Voltage
- Mass ↔ Capacitance, Spring compliance ↔ Inductance
- **Topology preserved** (series stays series, parallel stays parallel)
- Chain analogy would be: chain tension ↔ current (through variable), chain speed ↔ voltage (across variable)

The mobility analogy is interesting for chain visualisation because the chain physically passes *through* components (like a through variable), and tension is an *across* variable measured between nodes. The code's choice of impedance analogy (speed ↔ current) means speed is the through variable — which matches the chain intuition better for series connections.

## Sources

1. Wikipedia — «Mechanical–electrical analogies»
   URL: https://en.wikipedia.org/wiki/Mechanical%E2%80%93electrical_analogies
   Coverage: impedance vs mobility analogies, power conjugate variables, Hamiltonian variables, rotational variants, history (Maxwell 1873, Firestone 1933).

2. Wikipedia — «Impedance analogy»
   URL: https://en.wikipedia.org/wiki/Impedance_analogy
   Coverage: canonical element mapping table (damper↔resistor, mass↔inductor, spring compliance↔capacitor), topology flip (series↔parallel), power/energy equations, transducer gyrator model, history.

3. Wikipedia — «Mobility analogy»
   URL: https://en.wikipedia.org/wiki/Mobility_analogy
   Coverage: dual of impedance analogy, topology preservation, force↔current, mass↔capacitance, compliance↔inductance.

4. Codebase sources (read-only research, no modifications):
   - `src/physics/ChainSim.h` — ChainSpec, ChainSim API
   - `src/physics/ChainSim.cpp` — Oval racetrack, Loop, brake logic (line 104-105)
   - `src/projection/MechanicsMapping.h` — canonical mapping functions, power/energy identities
   - `src/projection/ProjectionBuilder.cpp` — emitSpring (line 1186-1232), emitFlywheel (line 1234-1270), emitBrake (line 1039-1086), emitCrank (line 1130-1172), emitChain (line 917-1037)
   - `docs/MECHANICS_PROJECTION.md` — analogy table, power correspondence, design decisions
   - `docs/MECHANICS_CHAIN_TEST_PLAN.md` — endurance test plan for chain motion
   - `docs/HANDOFF_MECHANICS_SOURCE_TANGENT_CHAIN.md` — source drive sprocket implementation

## Counters

- Findings tagged: 6
- DISCREPANCY: 2
- PHYSICS-BUG: 1
- IDEA: 3
- Severity distribution: Medium=1, Low=2, Info=3
