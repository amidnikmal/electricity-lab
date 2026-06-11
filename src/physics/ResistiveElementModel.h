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
