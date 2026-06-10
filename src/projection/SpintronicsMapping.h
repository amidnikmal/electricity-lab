#pragma once

#include "circuit/Circuit.h"
#include "physics/PowerModel.h"

// Electrical -> mechanical (spintronics-like) analogy map. Pure functions.
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
namespace current_lab::spintronics {

constexpr double kTensionPerVolt = 1.0;  // tension units per volt
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

// Spring displacement is proportional to Vc (charge q = C*Vc <-> compression).
inline double springCompressionFromVoltage(double capVoltage) {
    return capVoltage;
}

// Flywheel angular momentum is proportional to Il (flux linkage = L*Il).
inline double flywheelAngularMomentumFromCurrent(double indCurrent) {
    return indCurrent;
}

inline double springEnergy(double capacitance, double capVoltage) {
    return 0.5 * capacitance * capVoltage * capVoltage; // == 1/2 C V^2
}

inline double flywheelEnergy(double inductance, double indCurrent) {
    return 0.5 * inductance * indCurrent * indCurrent; // == 1/2 L I^2
}

} // namespace current_lab::spintronics
