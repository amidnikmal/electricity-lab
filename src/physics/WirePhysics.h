#pragma once

#include "physics/PhysicalUnits.h"
#include <algorithm>
#include <cmath>

namespace current_lab::physics {

inline double wireResistance(double length, double resistancePerUnit = kDefaultWireResistancePerUnit) {
    return std::max(0.0, length) * resistancePerUnit;
}

inline double segmentResistance(double length,
                                int segments,
                                double resistancePerUnit = kDefaultWireResistancePerUnit) {
    if (segments <= 0) return 0.0;
    return wireResistance(length, resistancePerUnit) / static_cast<double>(segments);
}

inline double electricFieldMagnitude(double voltageDrop, double length) {
    if (length <= kMinimumPhysicalLength) return 0.0;
    return std::abs(voltageDrop) / length;
}

inline double linearPotentialAt(double vA, double vB, double t) {
    double clampedT = std::clamp(t, 0.0, 1.0);
    return vA + (vB - vA) * clampedT;
}

} // namespace current_lab::physics
