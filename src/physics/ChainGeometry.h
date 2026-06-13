#pragma once

#include <algorithm>
#include <cmath>

// Single source of truth for chain <-> sprocket engagement geometry in the
// Mechanics view. The Box2D loop path (ChainSim), the chain renderer and the
// gear renderer all derive their dimensions from these pure functions.
// History: each of the three used its own constants, so the simulated chain
// arced around a node at ~0.26*wireThickness while the gear was drawn at
// ~0.72*wireThickness — the chain visually "slipped off" the sprockets.
namespace current_lab::physics::chain_geometry {

inline constexpr double kPi = 3.14159265358979323846;

// Roller (link) radius for a given view wire thickness.
inline double linkRadius(double wireThickness) {
    return std::max(1.0, wireThickness * 0.16);
}

// Distance between neighbouring rollers along the loop (chain pitch).
inline double linkPitch(double linkRadius) { return linkRadius * 2.6; }

// Half-width of the chain run (lateral clearance of the guide rails).
inline double chainHalfWidth(double wireThickness) { return wireThickness * 0.5; }

// Pitch radius: chain rollers ride exactly on this circle around a node, and
// the sprocket is drawn so its tooth gaps lie on the same circle. This single
// number is what keeps the chain ON the gears.
inline double sprocketPitchRadius(double halfWidth, double linkRadius) {
    return std::max(halfWidth, linkRadius * 2.2);
}

// Voltage sources are drive sprockets. The pitch circle is only slightly proud
// of the straight chain runs: enough for real contact, but not so large that
// the belt looks glued around the gear instead of pulled taut.
inline double driveSprocketPitchRadius(double halfWidth, double linkRadius) {
    double nodePitch = sprocketPitchRadius(halfWidth, linkRadius);
    return std::max({nodePitch * 1.45, halfWidth * 1.45, linkRadius * 3.0});
}

// On the source gear, positive chain travel across the upper run moves over
// the pitch circle clockwise, so the gear angle must decrease for no slip.
inline double sourceDriveSprocketPhaseFromChainTravel(double chainTravel,
                                                      double pitchRadius) {
    return pitchRadius > 1e-9 ? -chainTravel / pitchRadius : 0.0;
}

// Tooth tips reach slightly past the rollers; the disc body sits under them.
inline double sprocketTipRadius(double pitchRadius, double linkRadius) {
    return pitchRadius + linkRadius * 0.9;
}

inline double sprocketRootRadius(double pitchRadius, double linkRadius) {
    return std::max(pitchRadius - linkRadius * 0.9, pitchRadius * 0.4);
}

// Tooth count such that the angular tooth pitch on the pitch circle matches
// the chain link pitch: one roller per tooth gap (real sprocket engagement).
inline int sprocketTeeth(double pitchRadius, double linkPitch) {
    return std::max(6, static_cast<int>(std::lround(2.0 * kPi * pitchRadius / linkPitch)));
}

// Bicycle-chain plate line widths (world units). Shared with the renderer so
// tests can identify outer/inner plates among emitted primitives.
inline constexpr double kOuterPlateWidth = 1.7;
inline constexpr double kInnerPlateWidth = 1.3;

} // namespace current_lab::physics::chain_geometry
