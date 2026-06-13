#pragma once

#include "math/Vec2.h"
#include "physics/WirePhysics.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace current_lab::physics
{

enum class VisualMaterial
{
    ConductiveLead,
    ResistiveBody,
};

struct ConductivePathSection
{
    Vec2 start;
    Vec2 end;
    double halfWidth = 0.0;
    double voltageStart = 0.0;
    double voltageEnd = 0.0;
    VisualMaterial material = VisualMaterial::ConductiveLead;
    double relativeResistance = 0.0;
    double driftSpeedScale = 1.0;

    double length() const { return (end - start).length(); }
};

inline double resistorBodyLength(double terminalLength, double wireThickness)
{
    if (terminalLength <= kMinimumPhysicalLength)
        return 0.0;

    double body = std::clamp(terminalLength * 0.18, wireThickness * 3.0, wireThickness * 6.0);
    return std::min(body, terminalLength * 0.45);
}

inline double resistorBodyHalfWidth(double wireThickness)
{
    return std::max(wireThickness * 0.55, wireThickness * 1.1);
}

inline double driftSpeedScaleFromHalfWidth(double referenceHalfWidth, double sectionHalfWidth)
{
    if (referenceHalfWidth <= kMinimumPhysicalLength || sectionHalfWidth <= kMinimumPhysicalLength)
        return 1.0;

    // 2D educational display: wider material means lower visual current density.
    return std::clamp(referenceHalfWidth / sectionHalfWidth, 0.15, 4.0);
}

inline std::vector<ConductivePathSection> resistorPathSections(Vec2 a,
                                                               Vec2 b,
                                                               double voltageA,
                                                               double voltageB,
                                                               double wireThickness)
{
    std::vector<ConductivePathSection> sections;
    Vec2 axis = b - a;
    double terminalLength = axis.length();
    if (terminalLength <= kMinimumPhysicalLength)
        return sections;

    Vec2 unit = axis / terminalLength;
    double wireHalfWidth = std::max(0.5, wireThickness * 0.5);
    double bodyLength = resistorBodyLength(terminalLength, wireThickness);
    if (bodyLength <= kMinimumPhysicalLength) {
        sections.push_back({a, b, resistorBodyHalfWidth(wireThickness), voltageA, voltageB,
                            VisualMaterial::ResistiveBody, 1.0,
                            driftSpeedScaleFromHalfWidth(wireHalfWidth, resistorBodyHalfWidth(wireThickness))});
        return sections;
    }

    Vec2 mid = a + axis * 0.5;
    Vec2 bodyStart = mid - unit * (bodyLength * 0.5);
    Vec2 bodyEnd = mid + unit * (bodyLength * 0.5);
    double bodyHalfWidth = resistorBodyHalfWidth(wireThickness);

    if ((bodyStart - a).length() > kMinimumPhysicalLength) {
        sections.push_back({a, bodyStart, wireHalfWidth, voltageA, voltageA,
                            VisualMaterial::ConductiveLead, 0.0, 1.0});
    }

    sections.push_back({bodyStart, bodyEnd, bodyHalfWidth, voltageA, voltageB,
                        VisualMaterial::ResistiveBody, 1.0,
                        driftSpeedScaleFromHalfWidth(wireHalfWidth, bodyHalfWidth)});

    if ((b - bodyEnd).length() > kMinimumPhysicalLength) {
        sections.push_back({bodyEnd, b, wireHalfWidth, voltageB, voltageB,
                            VisualMaterial::ConductiveLead, 0.0, 1.0});
    }

    return sections;
}

// Axial interval of the resistive body along the a->b channel. The Drude
// scatterer lattice in ParticleSim must live exactly here: an invisible
// pillar under a section drawn as a plain lead reads as electrons bouncing
// off nothing (review finding 2026-06-12).
struct AxialSpan
{
    double start = 0.0;
    double end = 0.0;
};

inline AxialSpan resistorBodySpan(double channelLength, double wireThickness)
{
    double bodyLength = resistorBodyLength(channelLength, wireThickness);
    if (bodyLength <= kMinimumPhysicalLength)
        return {0.0, channelLength}; // degenerate: the whole channel is body
    double mid = channelLength * 0.5;
    return {mid - bodyLength * 0.5, mid + bodyLength * 0.5};
}

// Hydraulic resistor = a venturi (converging-diverging nozzle). The Box2D
// collider walls (ParticleSim, water world) AND the drawn funnel
// (ProjectionBuilder, Hydraulic view) MUST follow this ONE profile — otherwise
// the water flows in a wider channel than the throat drawn around it and the
// balls ride outside the necked walls (user finding 2026-06-13). Same lesson
// as ChainGeometry / resistorBodySpan: never compute the shape twice.
//
// Throat fraction of the pipe half-width. 0.6 is a clearly visible necking that
// the dense granular pack still passes: a SMOOTH throat arches far less than the
// old staggered Drude pillars did at the same minimum width. Tuned to keep all
// WaterNetwork.* (flow conservation / temporal uniformity / incompressibility)
// green — widen if a tighter throat clogs.
inline constexpr double kHydraulicThroatFraction = 0.6;

struct HydraulicThrottle
{
    double leadIn = 0.0;     // axial t (from channel start a) where the inlet cone begins
    double throatStart = 0.0;
    double throatEnd = 0.0;
    double leadOut = 0.0;    // axial t where the outlet cone ends
    double mouthHalfWidth = 0.0;  // = pipe (channel) half-width
    double throatHalfWidth = 0.0; // narrowed throat

    // Channel half-width at axial position t (piecewise linear: lead -> cone ->
    // throat -> cone -> lead). Outside [leadIn, leadOut] it is the full pipe.
    double halfWidthAt(double t) const
    {
        if (t <= leadIn || t >= leadOut) return mouthHalfWidth;
        if (t >= throatStart && t <= throatEnd) return throatHalfWidth;
        if (t < throatStart) {
            double f = (t - leadIn) / std::max(throatStart - leadIn, 1e-9);
            return mouthHalfWidth + (throatHalfWidth - mouthHalfWidth) * f;
        }
        double f = (t - throatEnd) / std::max(leadOut - throatEnd, 1e-9);
        return throatHalfWidth + (mouthHalfWidth - throatHalfWidth) * f;
    }
};

inline HydraulicThrottle hydraulicThrottle(double channelLength, double halfWidth)
{
    AxialSpan body = resistorBodySpan(channelLength, halfWidth * 2.0);
    HydraulicThrottle th;
    th.mouthHalfWidth = halfWidth;
    th.throatHalfWidth = halfWidth * kHydraulicThroatFraction;
    double bodyLen = body.end - body.start;
    // Long gentle cones + a SHORT central pinch: gradual convergence eases the
    // granular pack in, the brief narrow run avoids a long single-file column
    // that would clog.
    double throatLen = std::min(halfWidth * 2.0, bodyLen * 0.35);
    double cone = std::max(0.0, (bodyLen - throatLen) * 0.5);
    th.leadIn = body.start;
    th.throatStart = body.start + cone;
    th.throatEnd = body.end - cone;
    th.leadOut = body.end;
    if (th.throatEnd < th.throatStart) // degenerate short body: single throat point
        th.throatStart = th.throatEnd = (body.start + body.end) * 0.5;
    return th;
}

// The drawn funnel wall sits this far OUTSIDE the collider wall — the same gap
// the plain pipe already uses (drawn 0.55*wt vs collider 0.5*wt) — so a ball
// resting against the collider is still just inside the drawn wall.
inline double hydraulicWallDrawMargin(double halfWidth) { return halfWidth * 0.1; }

inline double resistorBodyElectricFieldMagnitude(Vec2 a,
                                                 Vec2 b,
                                                 double voltageA,
                                                 double voltageB,
                                                 double wireThickness)
{
    double bodyLength = resistorBodyLength((b - a).length(), wireThickness);
    return electricFieldMagnitude(voltageA - voltageB, bodyLength);
}

} // namespace current_lab::physics
