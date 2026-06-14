#include <gtest/gtest.h>

#include <cmath>

#include "circuit/Circuit.h"
#include "physics/ChainGeometry.h"
#include "projection/MechanicsCapacitor.h"
#include "projection/ProjectionBuilder.h"
#include "solver/CircuitSolver.h"

// Spring capacitor: pure kinematics (sign invariant, deformation) + the
// render-without-side-effects contract through buildProjection.
namespace {

using current_lab::mechanics::SpringCapacitorModel;
using current_lab::mechanics::capacitorThetaFromVoltage;
using namespace current_lab::projection;

SpringCapacitorModel modelAt(double theta) {
    SpringCapacitorModel m;
    m.theta = theta;
    return m;
}

} // namespace

// theta<0 stretches (charge+), theta>0 compresses (charge−), theta=0 neutral.
TEST(SpringCapacitor, ChargeAndDeflectionSignInvariant) {
    SpringCapacitorModel neutral = modelAt(0.0);
    EXPECT_NEAR(neutral.deflection(), 0.0, 1e-9);
    EXPECT_NEAR(neutral.charge(), 0.0, 1e-9);
    EXPECT_EQ(neutral.mode(), SpringCapacitorModel::Mode::Neutral);

    SpringCapacitorModel stretched = modelAt(-0.6);
    EXPECT_LT(stretched.deflection(), 0.0);          // longer than rest
    EXPECT_GT(stretched.charge(), 0.0);              // charge positive
    EXPECT_GT(stretched.springLength(), stretched.restLength());
    EXPECT_EQ(stretched.mode(), SpringCapacitorModel::Mode::Stretched);

    SpringCapacitorModel compressed = modelAt(+0.6);
    EXPECT_GT(compressed.deflection(), 0.0);         // shorter than rest
    EXPECT_LT(compressed.charge(), 0.0);             // charge negative
    EXPECT_LT(compressed.springLength(), compressed.restLength());
    EXPECT_EQ(compressed.mode(), SpringCapacitorModel::Mode::Compressed);
}

TEST(SpringCapacitor, EnergyGrowsWithDeflectionAndIsEven) {
    EXPECT_GT(modelAt(0.8).energy(), modelAt(0.4).energy());
    // Energy ~ x^2: symmetric in the charge sign.
    EXPECT_NEAR(modelAt(0.5).energy(), modelAt(-0.5).energy(), 1e-9);
    EXPECT_NEAR(modelAt(0.0).energy(), 0.0, 1e-12);
}

TEST(SpringCapacitor, CoilSpacingTracksLengthAndPathIsDeterministic) {
    SpringCapacitorModel stretched = modelAt(-0.7);
    SpringCapacitorModel compressed = modelAt(+0.7);

    auto span = [](const SpringCapacitorModel& m) {
        auto pts = m.springPath();
        return (pts.back() - pts.front()).length();
    };
    // A stretched spring spans farther than a compressed one (coils spread).
    EXPECT_GT(span(stretched), span(compressed));

    // Pure: same theta -> byte-identical path (no hidden state).
    auto p1 = modelAt(0.3).springPath();
    auto p2 = modelAt(0.3).springPath();
    ASSERT_EQ(p1.size(), p2.size());
    for (size_t i = 0; i < p1.size(); ++i) {
        EXPECT_DOUBLE_EQ(p1[i].x, p2[i].x);
        EXPECT_DOUBLE_EQ(p1[i].y, p2[i].y);
    }
}

TEST(SpringCapacitor, PositiveVoltageCompresses) {
    double thetaPos = capacitorThetaFromVoltage(5.0, 5.0);
    double thetaNeg = capacitorThetaFromVoltage(-5.0, 5.0);
    EXPECT_GT(thetaPos, 0.0);                  // +Vc -> compression
    EXPECT_LT(thetaNeg, 0.0);
    EXPECT_DOUBLE_EQ(capacitorThetaFromVoltage(0.0, 5.0), 0.0);
    // Saturates at +/- thetaMax.
    SpringCapacitorModel m;
    EXPECT_NEAR(capacitorThetaFromVoltage(1000.0, 5.0, m.p.thetaMax), m.p.thetaMax, 1e-9);
}

namespace {
const std::vector<Vec2>* longestPoly(const ProjectionResult& r) {
    const std::vector<Vec2>* best = nullptr;
    double bestLen = -1.0;
    for (const auto& poly : r.prims.polylines) {
        if (poly.pts.size() < 5) continue;
        double s = (poly.pts.back() - poly.pts.front()).length();
        if (s > bestLen) { bestLen = s; best = &poly.pts; }
    }
    return best;
}
} // namespace

// Fix #1: the spring ends exactly on the crank attachment knobs (no gap). The
// spring polyline's first/last points must coincide pixel-for-pixel with a
// filled attachment circle.
TEST(SpringCapacitor, SpringEndpointsPinnedToCrankKnobs) {
    Circuit c;
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(240, 0));
    c.addComponent(ComponentType::Capacitor, n1, n2, 1e-3);

    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    ViewParams p;
    ProjectionResult r = buildProjection(ProjectionKind::Mechanical, c, &sol, p);

    const auto* spring = longestPoly(r);
    ASSERT_NE(spring, nullptr);

    auto hasFilledCircleAt = [&](Vec2 pt) {
        for (const auto& circ : r.prims.circles)
            if (circ.filled && (circ.center - pt).length() < 1e-9) return true;
        return false;
    };
    EXPECT_TRUE(hasFilledCircleAt(spring->front())) << "spring start detached from its knob";
    EXPECT_TRUE(hasFilledCircleAt(spring->back())) << "spring end detached from its knob";
}

// The capacitor's lead chains run on the loop's chainTravel — the SAME clock as
// every other component — so they roll in lockstep with the system, NOT on the
// voltage. Feeding a different chainTravel for the capacitor must move its
// rollers (and feeding the system value keeps them synced with the neighbours).
TEST(SpringCapacitor, ChainRollsWithLoopTravelNotVoltage) {
    Circuit c;
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(240, 0));
    int capId = c.addComponent(ComponentType::Capacitor, n1, n2, 1e-3);

    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    ViewParams p;
    double rollerR = current_lab::physics::chain_geometry::linkRadius(p.wireThickness);

    auto rollerCenters = [&](double travel) {
        std::unordered_map<int, double> ct{{capId, travel}};
        p.chainTravel = &ct;
        ProjectionResult r = buildProjection(ProjectionKind::Mechanical, c, &sol, p);
        std::vector<Vec2> out;
        for (const auto& circ : r.prims.circles)
            if (circ.filled && std::abs(circ.radius - rollerR) < 1e-9)
                out.push_back(circ.center);
        return out;
    };

    auto a = rollerCenters(0.0);
    auto b = rollerCenters(8.0); // loop advanced the chain
    ASSERT_FALSE(a.empty());
    ASSERT_EQ(a.size(), b.size());
    bool moved = false;
    for (size_t i = 0; i < a.size(); ++i)
        if ((a[i] - b[i]).length() > 1e-6) { moved = true; break; }
    EXPECT_TRUE(moved) << "capacitor lead chain did not roll with the loop travel";
}

// Render-without-side-effects: building the same circuit twice yields an
// identical capacitor spring polyline (no hidden render state). DC steady, so
// no time-driven animation perturbs the result.
TEST(SpringCapacitor, RenderHasNoHiddenState) {
    Circuit c;
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(240, 0));
    c.addComponent(ComponentType::Capacitor, n1, n2, 1e-3);

    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    ViewParams p;

    ProjectionResult r1 = buildProjection(ProjectionKind::Mechanical, c, &sol, p);
    ProjectionResult r2 = buildProjection(ProjectionKind::Mechanical, c, &sol, p);
    const auto* s1 = longestPoly(r1);
    const auto* s2 = longestPoly(r2);
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    ASSERT_EQ(s1->size(), s2->size());
    for (size_t i = 0; i < s1->size(); ++i) {
        EXPECT_DOUBLE_EQ((*s1)[i].x, (*s2)[i].x);
        EXPECT_DOUBLE_EQ((*s1)[i].y, (*s2)[i].y);
    }
}
