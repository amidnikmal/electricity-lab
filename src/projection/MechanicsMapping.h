#pragma once

#include "circuit/Circuit.h"
#include <algorithm>
#include "physics/PowerModel.h"

// Electrical -> mechanical (mechanical) analogy map. Pure functions.
//
//   current I        -> chain linear speed (sign = direction of motion)
//   potential V      -> chain tension ("height" relative to the anchor)
//   resistor R       -> friction brake (dissipates power as heat)
//   voltage source   -> drive crank (injects energy, sets tension difference)
//   capacitor C      -> spring (energy in displacement; charge <-> compression)
//   inductor L       -> flywheel (energy in motion; current <-> angular momentum)
//   diode            -> ratchet (chain moves one way only)
//   ground           -> fixed anchor (reference tension)
//
// Unit choice: kTensionPerVolt * kLinkSpeedPerAmp == 1, so mechanical power
// tension * speed equals electrical power V * I exactly. Quantities below are
// exact images of solver values; only on-screen animation speed is scaled.
namespace current_lab::mechanics {

constexpr double kTensionPerVolt = 1.0;
// Visual amplification constants (animation only, magnitudes stay honest):
constexpr double kVisualChainSpeed = 40.0; // world units per (chain-speed unit * s)
// The single "speed knob" for the whole mechanics view (see MainWindow). One
// uniform factor on every component so the capacitor's chain/gear/arm/spring
// stay one rigid body in sync with the loop; tune here for faster/calmer motion.
constexpr double kMechChainBoost = 20.0;
// Visual gain from net charge (= capacitor chain travel) to spring compression
// in world units. Only the AMPLITUDE of the in-line spring; it stays phase-locked
// to the chain (both ∝ chainTravel), so the spring breathes in sync with the loop.
constexpr double kMechSpringCompress = 6.0;
constexpr double kVisualSpinRate = 6.0;    // rad per (angular-momentum unit * s)  // tension units per volt
constexpr double kLinkSpeedPerAmp = 1.0; // chain speed units per ampere

inline double chainSpeedFromCurrent(double current) {
    return kLinkSpeedPerAmp * current; // signed: direction follows current sign
}

inline double tensionFromPotential(double potential) {
    return kTensionPerVolt * potential; // signed, relative to the anchor node
}

inline double mechanicalPower(double tension, double speed) {
    return tension * speed;
}

inline double brakeHeatFromPower(ComponentType type, double power) {
    return physics::dissipatedPowerOnly(type, power);
}

// Сжатие пружины ∝ заряду q = C·Vc (не просто Vc).
// Ёмкость C передаётся из comp.value при вызове emitSpring.
inline double springCompressionFromVoltage(double capacitance, double capVoltage) {
    return capacitance * capVoltage; // q = C·Vc
}

// Flywheel angular momentum is proportional to Il (flux linkage = L*Il).
inline double flywheelAngularMomentumFromCurrent(double indCurrent) {
    return indCurrent;
}

inline double springEnergy(double capacitance, double capVoltage) {
    return 0.5 * capacitance * capVoltage * capVoltage; // == 1/2 C V^2
}

// Hand-crank dynamo: turning the drive crank sets the source EMF.
// V is proportional to the crank angular speed (clamped to a sane range).
inline double emfFromCrankSpeed(double omegaRadPerSec) {
    double v = 1.5 * omegaRadPerSec;
    return std::clamp(v, -12.0, 12.0);
}

inline double flywheelEnergy(double inductance, double indCurrent) {
    return 0.5 * inductance * indCurrent * indCurrent; // == 1/2 L I^2
}

} // namespace current_lab::mechanics
