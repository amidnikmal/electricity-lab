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
// The spring is slung between the two crank-arm tips and pinned pixel-exact to
// the attachment knobs (no gap): its endpoints coincide with filled knob circles.
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
    EXPECT_TRUE(hasFilledCircleAt(spring->front())) << "spring start detached from its arm knob";
    EXPECT_TRUE(hasFilledCircleAt(spring->back())) << "spring end detached from its arm knob";
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

// SYNCHRONY (point 3): the spring compression and the gear rotation are driven
// by the SAME loop travel, so they move together AND flip together with the
// current direction — the RLC-ring requirement. Charging (+travel) compresses
// and turns the gear one way; reversing the travel stretches and turns it back.
TEST(SpringCapacitor, SpringCompressionAndGearTrackTravelTogether) {
    namespace cg = current_lab::physics::chain_geometry;
    Circuit c;
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(240, 0));
    int capId = c.addComponent(ComponentType::Capacitor, n1, n2, 1e-3);

    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    ViewParams p;
    const double rollerR = cg::linkRadius(p.wireThickness);
    const double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(p.wireThickness), rollerR);
    const double rootR = cg::sprocketRootRadius(pitchR, rollerR);
    const double tipR = cg::sprocketTipRadius(pitchR, rollerR);
    const int teeth = cg::sprocketTeeth(pitchR, cg::linkPitch(rollerR));
    const double toothPitch = 2.0 * cg::kPi / teeth;

    auto wrap = [](double d, double period) {
        while (d > period * 0.5) d -= period;
        while (d < -period * 0.5) d += period;
        return d;
    };
    struct Sample { double springLen; double gearPhase; };
    auto sample = [&](double travel) {
        std::unordered_map<int, double> ct{{capId, travel}};
        p.chainTravel = &ct;
        ProjectionResult r = buildProjection(ProjectionKind::Mechanical, c, &sol, p);
        const auto* spring = longestPoly(r);
        EXPECT_NE(spring, nullptr);
        double springLen = std::abs(spring->back().x - spring->front().x);
        // Any shaft sprocket (radius rootR): its tooth circular mean = rotation.
        double sx = 0, sy = 0; int n = 0;
        Vec2 center{};
        for (const auto& circ : r.prims.circles)
            if (circ.filled && std::abs(circ.radius - rootR) < 1e-9) { center = circ.center; break; }
        for (const auto& q : r.prims.quads) {
            if (!q.filled) continue;
            Vec2 cen((q.p1.x + q.p2.x + q.p3.x + q.p4.x) * 0.25,
                     (q.p1.y + q.p2.y + q.p3.y + q.p4.y) * 0.25);
            double d = (cen - center).length();
            if (d < rootR * 0.9 || d > tipR + 1e-9) continue;
            double a = std::atan2(cen.y - center.y, cen.x - center.x);
            sx += std::cos(teeth * a); sy += std::sin(teeth * a); ++n;
        }
        return Sample{springLen, std::atan2(sy, sx) / teeth};
    };

    Sample neu = sample(0.0);
    Sample pos = sample(+0.25 * pitchR);
    Sample neg = sample(-0.25 * pitchR);

    // Charging compresses (shorter), discharging the other way stretches (longer).
    EXPECT_LT(pos.springLen, neu.springLen - 1e-6) << "+travel did not compress the spring";
    EXPECT_GT(neg.springLen, neu.springLen + 1e-6) << "−travel did not stretch the spring";
    // The gear turns the opposite way for the opposite travel — synchronous sign.
    double dPos = wrap(pos.gearPhase - neu.gearPhase, toothPitch);
    double dNeg = wrap(neg.gearPhase - neu.gearPhase, toothPitch);
    EXPECT_GT(std::abs(dPos), 0.02);
    EXPECT_LT(dPos * dNeg, 0.0) << "gear did not reverse with the travel — not synchronous";
}

// STRICT (point 1): every sprocket sits on the one loop chain, so NO gear may
// turn at a rate different from the system. In a series RC advance the loop
// travel and assert every rendered node/shaft sprocket rotates by the SAME
// angle. (Catches a capacitor shaft gear that spins against the loop.)
TEST(SpringCapacitor, NoGearTurnsOutOfSyncWithTheLoop) {
    namespace cg = current_lab::physics::chain_geometry;
    Circuit c;
    int gnd = c.addNode(Vec2(0, 200));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    int src = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    int res = c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
    int cap = c.addComponent(ComponentType::Capacitor, n2, gnd, 1e-3);

    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    ViewParams p;
    const double rollerR = cg::linkRadius(p.wireThickness);
    const double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(p.wireThickness), rollerR);
    const double rootR = cg::sprocketRootRadius(pitchR, rollerR);
    const double tipR = cg::sprocketTipRadius(pitchR, rollerR);
    const int teeth = cg::sprocketTeeth(pitchR, cg::linkPitch(rollerR));
    const double toothPitch = 2.0 * cg::kPi / teeth;

    // Every node/shaft sprocket = a filled body circle of radius rootR; its
    // rotation is the circular mean of its tooth centroids.
    auto sprockets = [&](double travel) {
        std::unordered_map<int, double> ct{{src, travel}, {res, travel}, {cap, travel}};
        p.chainTravel = &ct;
        ProjectionResult r = buildProjection(ProjectionKind::Mechanical, c, &sol, p);
        std::vector<std::pair<Vec2, double>> out;
        for (const auto& circ : r.prims.circles) {
            if (!circ.filled || std::abs(circ.radius - rootR) > 1e-9) continue;
            double sx = 0, sy = 0; int n = 0;
            for (const auto& q : r.prims.quads) {
                if (!q.filled) continue;
                Vec2 cen((q.p1.x + q.p2.x + q.p3.x + q.p4.x) * 0.25,
                         (q.p1.y + q.p2.y + q.p3.y + q.p4.y) * 0.25);
                double d = (cen - circ.center).length();
                if (d < rootR * 0.9 || d > tipR + 1e-9) continue;
                double a = std::atan2(cen.y - circ.center.y, cen.x - circ.center.x);
                sx += std::cos(teeth * a); sy += std::sin(teeth * a); ++n;
            }
            if (n == teeth) out.push_back({circ.center, std::atan2(sy, sx) / teeth});
        }
        return out;
    };

    auto g0 = sprockets(0.0);
    auto g1 = sprockets(0.25 * pitchR);
    ASSERT_GE(g0.size(), 2u) << "expected several gears in a series RC";
    ASSERT_EQ(g0.size(), g1.size());

    auto wrap = [](double d, double period) {
        while (d > period * 0.5) d -= period;
        while (d < -period * 0.5) d += period;
        return d;
    };
    // SPEED, not signed angle: every gear turns at the loop speed. Two separate
    // capacitor shafts may counter-rotate (different axles), but no gear may turn
    // at a different |rate| than the loop.
    std::vector<double> speeds;
    for (const auto& [center, ph0] : g0) {
        double ph1 = 0; bool found = false;
        for (const auto& [c1, p1] : g1)
            if ((c1 - center).length() < 1e-9) { ph1 = p1; found = true; break; }
        ASSERT_TRUE(found);
        speeds.push_back(std::abs(wrap(ph1 - ph0, toothPitch)));
    }
    double mn = speeds[0], mx = speeds[0];
    for (double d : speeds) { mn = std::min(mn, d); mx = std::max(mx, d); }
    EXPECT_GT(speeds[0], 0.02) << "nothing rotated — test is vacuous";
    EXPECT_LT(mx - mn, 1e-3)
        << "a gear turns at a different speed than the loop (spread " << (mx - mn) << " rad)";
}

// Баг «конденсатор в механике гуляет»: при МОНОТОННОМ заряде (рост chainTravel)
// пружина должна МОНОТОННО сжиматься. Со старым пределом 7.5 рад угол кривошипа
// проходил 90/180/270°, и длина пружины (deflection ∝ sin θ) то падала, то росла.
TEST(SpringCapacitor, SpringCompressesMonotonicallyWithCharge) {
    namespace cg = current_lab::physics::chain_geometry;
    Circuit c;
    int gnd = c.addNode(Vec2(0, 200));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
    int cap = c.addComponent(ComponentType::Capacitor, n2, gnd, 1e-3);

    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    ViewParams p;
    const double rollerR = cg::linkRadius(p.wireThickness);
    const double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(p.wireThickness), rollerR);
    Vec2 capMid = (c.findNode(n2)->position + c.findNode(gnd)->position) * 0.5;

    auto springSpan = [&](double travel) {
        std::unordered_map<int, double> ct{{cap, travel}};
        p.chainTravel = &ct;
        ProjectionResult r = buildProjection(ProjectionKind::Mechanical, c, &sol, p);
        // Пружина — единственная полилиния ширины 2.6; берём ближайшую к конденсатору.
        double best = -1.0, bestDist = 1e18;
        for (const auto& poly : r.prims.polylines) {
            if (std::abs(poly.width - 2.6) > 0.1 || poly.pts.size() < 2) continue;
            Vec2 mid = (poly.pts.front() + poly.pts.back()) * 0.5;
            double d = (mid - capMid).length();
            if (d < bestDist) { bestDist = d; best = (poly.pts.back() - poly.pts.front()).length(); }
        }
        return best;
    };

    double travels[] = {0.0, 0.5, 1.0, 2.0, 4.0, 8.0};
    double prev = 1e18;
    double first = -1.0, last = -1.0;
    for (double t : travels) {
        double span = springSpan(t * pitchR);
        ASSERT_GT(span, 0.0) << "пружина не найдена при travel=" << t;
        if (first < 0) first = span;
        last = span;
        EXPECT_LE(span, prev + 1e-6)
            << "длина пружины выросла при росте заряда (travel=" << t << ") — «гуляет»";
        prev = span;
    }
    EXPECT_LT(last, first * 0.9) << "пружина почти не сжалась при полном заряде";
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
