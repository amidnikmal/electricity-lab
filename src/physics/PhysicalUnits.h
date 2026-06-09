#pragma once

namespace current_lab::physics {

constexpr double kDefaultWireResistancePerUnit = 0.5; // Ohm / world unit
constexpr int kDefaultDistributedWireSegments = 8;
constexpr double kDefaultVisualDriftSpeedMultiplier = 18.0;
constexpr double kMinimumPhysicalLength = 1e-9;
constexpr double kPi = 3.14159265358979323846;
constexpr double kMu0 = 4e-7 * kPi; // Vacuum permeability, H/m

} // namespace current_lab::physics
