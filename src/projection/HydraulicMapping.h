#pragma once

#include "circuit/Circuit.h"
#include "physics/PowerModel.h"
#include <algorithm>
#include <cmath>

// Electrical -> hydraulic (water) analogy. Pure functions, exact images of
// solver values; only on-screen animation speed is scaled.
//
//   current I        -> volumetric flow rate Q (sign = flow direction)
//   potential V      -> pressure relative to the open reservoir (ground)
//   resistor R       -> narrow pipe (viscous restriction: Q = dP / R_hyd,
//                       the hydraulic Ohm's law / Poiseuille form)
//   voltage source   -> pump (holds a fixed pressure difference)
//   capacitor C      -> elastic tank (stored volume ∝ C * Vc; level ∝ Vc)
//   inductor L       -> turbine / long heavy pipe (inertance: dP = L dQ/dt)
//   diode            -> check valve (flow one way only)
//   switch           -> gate valve
//   ground           -> open reservoir (reference pressure)
//
// Unit choice: kFlowPerAmp * kPressurePerVolt == 1, so hydraulic power
// dP * Q equals electrical power V * I exactly.
namespace current_lab::hydraulic {

constexpr double kFlowPerAmp = 1.0;
constexpr double kPressurePerVolt = 1.0;

inline double flowFromCurrent(double current) {
    return kFlowPerAmp * current; // signed
}

inline double pressureFromPotential(double potential) {
    return kPressurePerVolt * potential; // signed, relative to the reservoir
}

inline double hydraulicPower(double pressureDrop, double flow) {
    return pressureDrop * flow;
}

inline double frictionHeatFromPower(ComponentType type, double power) {
    return physics::dissipatedPowerOnly(type, power);
}

// Tank fill level for the capacitor: the stored volume is proportional to
// C * Vc, so the level (at fixed tank cross-section) is proportional to Vc.
// Normalized by the circuit's voltage range for display.
inline double tankFillFraction(double capVoltage, double voltageRange) {
    double range = std::max(std::abs(voltageRange), 1e-9);
    return std::clamp(std::abs(capVoltage) / range, 0.0, 1.0);
}

inline double tankEnergy(double capacitance, double capVoltage) {
    return 0.5 * capacitance * capVoltage * capVoltage; // == 1/2 C V^2
}

inline double turbineEnergy(double inductance, double flow) {
    return 0.5 * inductance * flow * flow; // == 1/2 L I^2
}

} // namespace current_lab::hydraulic
