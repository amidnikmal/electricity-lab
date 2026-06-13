#pragma once

#include "math/Vec2.h"
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

// Voltage sources use a visibly larger drive sprocket. Chain tension is handled
// by tangent-path geometry, not by shrinking this wheel.
inline double driveSprocketPitchRadius(double halfWidth, double linkRadius) {
    double nodePitch = sprocketPitchRadius(halfWidth, linkRadius);
    return std::max({15.0, nodePitch * 2.8, halfWidth * 3.0, linkRadius * 8.0});
}

inline double sourceDriveSprocketPhaseFromChainTravel(double chainTravel,
                                                      double pitchRadius) {
    return pitchRadius > 1e-9 ? -chainTravel / pitchRadius : 0.0;
}

struct SourceDrivePath {
    bool valid = false;
    Vec2 a, b, center;
    double nodeRadius = 0.0;
    double driveRadius = 0.0;

    Vec2 aTop, driveLeftTop;
    Vec2 driveRightTop, bTop;
    Vec2 bBottom, driveRightBottom;
    Vec2 driveLeftBottom, aBottom;

    double aToDriveTop = 0.0;
    double driveTopArc = 0.0;
    double driveToBTop = 0.0;
    double bArc = 0.0;
    double bToDriveBottom = 0.0;
    double driveBottomArc = 0.0;
    double driveToABottom = 0.0;
    double aArc = 0.0;

    double perimeter = 0.0;
};

inline double angleOf(Vec2 v) { return std::atan2(v.y, v.x); }

inline double clockwiseDelta(double from, double to) {
    double d = from - to;
    while (d < 0.0) d += 2.0 * kPi;
    while (d >= 2.0 * kPi) d -= 2.0 * kPi;
    return d;
}

inline Vec2 circlePoint(Vec2 center, double radius, double angle) {
    return center + Vec2(std::cos(angle), std::sin(angle)) * radius;
}

inline Vec2 externalTangentNormal(Vec2 from, double fromRadius,
                                  Vec2 to, double toRadius, int side) {
    Vec2 d = to - from;
    double len = d.length();
    if (len < 1e-9) return {};

    Vec2 unit = d / len;
    Vec2 perp(-unit.y, unit.x);
    double h = std::clamp((fromRadius - toRadius) / len, -1.0, 1.0);
    double k = std::sqrt(std::max(0.0, 1.0 - h * h));
    return unit * h + perp * (side >= 0 ? k : -k);
}

inline double segmentLength(Vec2 a, Vec2 b) { return (b - a).length(); }

inline SourceDrivePath sourceDrivePath(Vec2 a, Vec2 b,
                                       double nodeRadius,
                                       double driveRadius) {
    SourceDrivePath path;
    path.a = a;
    path.b = b;
    path.center = (a + b) * 0.5;
    path.nodeRadius = nodeRadius;
    path.driveRadius = driveRadius;

    double halfLen = (path.center - a).length();
    if (nodeRadius <= 0.0 || driveRadius <= nodeRadius ||
        halfLen <= std::abs(driveRadius - nodeRadius) + 1e-6)
        return path;

    Vec2 nATop = externalTangentNormal(a, nodeRadius, path.center, driveRadius, 1);
    Vec2 nABottom = externalTangentNormal(a, nodeRadius, path.center, driveRadius, -1);
    Vec2 nBTop = externalTangentNormal(path.center, driveRadius, b, nodeRadius, 1);
    Vec2 nBBottom = externalTangentNormal(path.center, driveRadius, b, nodeRadius, -1);

    path.aTop = a + nATop * nodeRadius;
    path.driveLeftTop = path.center + nATop * driveRadius;
    path.aBottom = a + nABottom * nodeRadius;
    path.driveLeftBottom = path.center + nABottom * driveRadius;

    path.driveRightTop = path.center + nBTop * driveRadius;
    path.bTop = b + nBTop * nodeRadius;
    path.driveRightBottom = path.center + nBBottom * driveRadius;
    path.bBottom = b + nBBottom * nodeRadius;

    double driveLeftTopA = angleOf(path.driveLeftTop - path.center);
    double driveRightTopA = angleOf(path.driveRightTop - path.center);
    double driveRightBottomA = angleOf(path.driveRightBottom - path.center);
    double driveLeftBottomA = angleOf(path.driveLeftBottom - path.center);
    double bTopA = angleOf(path.bTop - b);
    double bBottomA = angleOf(path.bBottom - b);
    double aBottomA = angleOf(path.aBottom - a);
    double aTopA = angleOf(path.aTop - a);

    path.aToDriveTop = segmentLength(path.aTop, path.driveLeftTop);
    path.driveTopArc = driveRadius * clockwiseDelta(driveLeftTopA, driveRightTopA);
    path.driveToBTop = segmentLength(path.driveRightTop, path.bTop);
    path.bArc = nodeRadius * clockwiseDelta(bTopA, bBottomA);
    path.bToDriveBottom = segmentLength(path.bBottom, path.driveRightBottom);
    path.driveBottomArc = driveRadius * clockwiseDelta(driveRightBottomA, driveLeftBottomA);
    path.driveToABottom = segmentLength(path.driveLeftBottom, path.aBottom);
    path.aArc = nodeRadius * clockwiseDelta(aBottomA, aTopA);

    path.perimeter = path.aToDriveTop + path.driveTopArc + path.driveToBTop +
                     path.bArc + path.bToDriveBottom + path.driveBottomArc +
                     path.driveToABottom + path.aArc;
    path.valid = path.perimeter > 1e-6;
    return path;
}

inline Vec2 lerp(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }

inline Vec2 clockwiseCircleAt(Vec2 center, double radius,
                              double startAngle, double distance) {
    return circlePoint(center, radius, startAngle - distance / radius);
}

inline Vec2 sourceDrivePointAt(const SourceDrivePath& path, double t) {
    if (!path.valid) return {};
    t = std::fmod(t, path.perimeter);
    if (t < 0.0) t += path.perimeter;

    auto consume = [&](double len) {
        if (t <= len) return true;
        t -= len;
        return false;
    };

    if (consume(path.aToDriveTop))
        return lerp(path.aTop, path.driveLeftTop, t / path.aToDriveTop);

    double angle = angleOf(path.driveLeftTop - path.center);
    if (consume(path.driveTopArc))
        return clockwiseCircleAt(path.center, path.driveRadius, angle, t);

    if (consume(path.driveToBTop))
        return lerp(path.driveRightTop, path.bTop, t / path.driveToBTop);

    angle = angleOf(path.bTop - path.b);
    if (consume(path.bArc))
        return clockwiseCircleAt(path.b, path.nodeRadius, angle, t);

    if (consume(path.bToDriveBottom))
        return lerp(path.bBottom, path.driveRightBottom, t / path.bToDriveBottom);

    angle = angleOf(path.driveRightBottom - path.center);
    if (consume(path.driveBottomArc))
        return clockwiseCircleAt(path.center, path.driveRadius, angle, t);

    if (consume(path.driveToABottom))
        return lerp(path.driveLeftBottom, path.aBottom, t / path.driveToABottom);

    angle = angleOf(path.aBottom - path.a);
    return clockwiseCircleAt(path.a, path.nodeRadius, angle, t);
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
