#pragma once

#include "circuit/Circuit.h"
#include <algorithm>

namespace current_lab::physics {

inline double branchPower(double current, double voltageDrop) {
    return current * voltageDrop;
}

inline bool isSupplyingPower(double power) {
    return power < 0.0;
}

inline double dissipatedPowerOnly(ComponentType type, double power) {
    if (type != ComponentType::Resistor && type != ComponentType::Wire)
        return 0.0;
    return std::max(0.0, power);
}

inline double heatFraction(ComponentType type, double power, double maxDissipatedPower) {
    if (maxDissipatedPower <= 1e-12)
        return 0.0;
    return dissipatedPowerOnly(type, power) / maxDissipatedPower;
}

} // namespace current_lab::physics
