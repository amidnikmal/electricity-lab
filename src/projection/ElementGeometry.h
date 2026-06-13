#pragma once

#include "math/Vec2.h"
#include <algorithm>
#include <cmath>
#include <vector>

// World-space geometry of element symbols, shared by projections and tests.
namespace current_lab::projection {

struct CapacitorGeometry {
    bool valid = false;
    Vec2 unit, perp;
    Vec2 leadAEnd, leadBEnd; // where the leads stop and the plates start
    Vec2 plateATop, plateABottom;
    Vec2 plateBTop, plateBBottom;
    Vec2 mid;
    double gap = 0.0;
    double plateHalf = 0.0;
};

inline CapacitorGeometry capacitorGeometry(Vec2 a, Vec2 b, double wireThickness) {
    CapacitorGeometry g;
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return g;

    g.valid = true;
    g.unit = ab / len;
    g.perp = Vec2(-g.unit.y, g.unit.x);
    g.mid = a + ab * 0.5;
    g.gap = std::clamp(wireThickness * 1.2, 6.0, len * 0.4);
    g.plateHalf = std::max(wireThickness * 1.6, 14.0);
    g.leadAEnd = g.mid - g.unit * (g.gap * 0.5);
    g.leadBEnd = g.mid + g.unit * (g.gap * 0.5);
    g.plateATop = g.leadAEnd + g.perp * g.plateHalf;
    g.plateABottom = g.leadAEnd - g.perp * g.plateHalf;
    g.plateBTop = g.leadBEnd + g.perp * g.plateHalf;
    g.plateBBottom = g.leadBEnd - g.perp * g.plateHalf;
    return g;
}

struct InductorGeometry {
    bool valid = false;
    Vec2 unit, perp;
    Vec2 coilStart, coilEnd;
    int bumps = 4;
    double bumpRadius = 0.0;
};

inline InductorGeometry inductorGeometry(Vec2 a, Vec2 b, double wireThickness) {
    InductorGeometry g;
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return g;

    g.valid = true;
    g.unit = ab / len;
    g.perp = Vec2(-g.unit.y, g.unit.x);
    double coilLen = std::clamp(len * 0.4, wireThickness * 3.0, wireThickness * 8.0);
    coilLen = std::min(coilLen, len * 0.8);
    Vec2 mid = a + ab * 0.5;
    g.coilStart = mid - g.unit * (coilLen * 0.5);
    g.coilEnd = mid + g.unit * (coilLen * 0.5);
    g.bumps = 4;
    g.bumpRadius = coilLen / (2.0 * g.bumps);
    return g;
}

struct SwitchGeometry {
    bool valid = false;
    Vec2 unit, perp;
    Vec2 mid;
    Vec2 leadAEnd, leadBEnd; // contacts: where the leads stop and the gap starts
    double s = 0.0;          // glyph half-extent (lever length scale)
};

// Same formula as emitSwitchSymbol: the drawn glyph and the click target must
// be ONE geometry (the chain/sprocket lesson: never compute it twice).
inline SwitchGeometry switchGeometry(Vec2 a, Vec2 b) {
    SwitchGeometry g;
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return g;

    g.valid = true;
    g.unit = ab / len;
    g.perp = Vec2(-g.unit.y, g.unit.x);
    g.s = std::clamp(len * 0.25, 12.0, 40.0);
    g.mid = a + ab * 0.5;
    g.leadAEnd = g.mid - g.unit * (g.s * 0.5);
    g.leadBEnd = g.mid + g.unit * (g.s * 0.5);
    return g;
}

// Toggle hot-zone: click here flips the switch WITHOUT selecting it (the
// leads outside the zone still select as usual). Covers the gap and the
// open lever (it sticks out ~0.6*s perpendicular).
inline double switchToggleRadius(const SwitchGeometry& g, double wireThickness) {
    return std::max(g.s, wireThickness * 1.5);
}

// Half-circle arc points for one coil bump (above the axis).
inline std::vector<Vec2> inductorBumpArc(const InductorGeometry& g, int bumpIndex, int segments = 10) {
    std::vector<Vec2> pts;
    if (!g.valid || bumpIndex < 0 || bumpIndex >= g.bumps) return pts;

    Vec2 center = g.coilStart + g.unit * (g.bumpRadius * (2.0 * bumpIndex + 1.0));
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        double t = static_cast<double>(i) / segments; // 0..1 over half circle
        double angle = 3.14159265358979323846 * (1.0 - t);
        Vec2 offset = g.unit * (std::cos(angle) * g.bumpRadius) +
                      g.perp * (std::sin(angle) * g.bumpRadius);
        pts.push_back(center + offset);
    }
    return pts;
}

} // namespace current_lab::projection
