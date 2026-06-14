#include <gtest/gtest.h>

#include <cmath>

#include "circuit/Circuit.h"
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

    auto longestPoly = [](const ProjectionResult& r) {
        const std::vector<Vec2>* best = nullptr;
        double bestLen = -1.0;
        for (const auto& poly : r.prims.polylines) {
            if (poly.pts.size() < 5) continue;
            double s = (poly.pts.back() - poly.pts.front()).length();
            if (s > bestLen) { bestLen = s; best = &poly.pts; }
        }
        return best;
    };

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
