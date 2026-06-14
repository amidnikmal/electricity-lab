#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include "circuit/Circuit.h"
#include "physics/ChainGeometry.h"
#include "physics/ChainSim.h"
#include "projection/ProjectionBuilder.h"
#include "solver/CircuitSolver.h"

// Chain <-> sprocket engagement: the Box2D chain, the chain renderer and the
// gear renderer must agree on one geometry (regression 2026-06-11: "цепи
// слетели с шестерёнок" — the simulated chain arced around nodes far inside
// the drawn gear).
namespace {

namespace cg = current_lab::physics::chain_geometry;
using current_lab::physics::ChainLink;
using current_lab::physics::ChainSim;
using current_lab::physics::ChainSpec;
using namespace current_lab::projection;

// |distance from the racetrack pitch line|: 0 means the roller rides exactly
// on the straight rail or on the sprocket pitch circle around a node.
double pitchTrackDeviation(Vec2 pos, Vec2 a, Vec2 b, double pitchR) {
    Vec2 ab = b - a;
    double len = ab.length();
    Vec2 unit = ab / len;
    Vec2 rel = pos - a;
    double along = std::clamp(rel.x * unit.x + rel.y * unit.y, 0.0, len);
    Vec2 closest = a + unit * along;
    return std::abs((pos - closest).length() - pitchR);
}

ChainSpec wireSpec(Vec2 a, Vec2 b, double wireThickness, double target) {
    ChainSpec spec;
    spec.componentId = 1;
    spec.a = a;
    spec.b = b;
    spec.halfWidth = cg::chainHalfWidth(wireThickness);
    spec.targetSpeed = target;
    return spec;
}

ChainSpec sourceSpec(Vec2 a, Vec2 b, double wireThickness, double target) {
    ChainSpec spec = wireSpec(a, b, wireThickness, target);
    spec.driveSprocket = true;
    return spec;
}

double signedAngleDelta(double before, double after) {
    double delta = after - before;
    while (delta > cg::kPi) delta -= 2.0 * cg::kPi;
    while (delta < -cg::kPi) delta += 2.0 * cg::kPi;
    return delta;
}

double firstSprocketToothAngle(const ProjectionResult& res, Vec2 center,
                               double rootR, double tipR) {
    for (const auto& quad : res.prims.quads) {
        if (!quad.filled) continue;
        Vec2 centroid((quad.p1.x + quad.p2.x + quad.p3.x + quad.p4.x) * 0.25,
                      (quad.p1.y + quad.p2.y + quad.p3.y + quad.p4.y) * 0.25);
        Vec2 rel = centroid - center;
        double d = rel.length();
        if (d < rootR * 0.9 || d > tipR + 1e-9) continue;
        return std::atan2(rel.y, rel.x);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

void expectTangent(Vec2 center, Vec2 contact, Vec2 other) {
    Vec2 radial = (contact - center).normalized();
    Vec2 run = (other - contact).normalized();
    EXPECT_NEAR(radial.x * run.x + radial.y * run.y, 0.0, 1e-9);
}

} // namespace

// --- shared geometry invariants ----------------------------------------------

TEST(ChainGeometry, RadiiAreOrderedRootPitchTip) {
    for (double wt : {4.0, 8.0, 14.0, 24.0}) {
        double r = cg::linkRadius(wt);
        double half = cg::chainHalfWidth(wt);
        double pitchR = cg::sprocketPitchRadius(half, r);
        EXPECT_GE(pitchR, half) << "wt=" << wt;
        EXPECT_GT(cg::sprocketTipRadius(pitchR, r), pitchR) << "wt=" << wt;
        EXPECT_LT(cg::sprocketRootRadius(pitchR, r), pitchR) << "wt=" << wt;
        EXPECT_GT(cg::sprocketRootRadius(pitchR, r), 0.0) << "wt=" << wt;
    }
}

TEST(ChainGeometry, ToothPitchMatchesChainPitch) {
    // One roller per tooth gap: teeth * link pitch ~= pitch circumference.
    for (double wt : {4.0, 8.0, 14.0, 24.0}) {
        double r = cg::linkRadius(wt);
        double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(wt), r);
        int teeth = cg::sprocketTeeth(pitchR, cg::linkPitch(r));
        EXPECT_GE(teeth, 6);
        double circumference = 2.0 * cg::kPi * pitchR;
        EXPECT_NEAR(teeth * cg::linkPitch(r), circumference, circumference * 0.15)
            << "wt=" << wt;
    }
}

TEST(ChainGeometry, TeethCountGrowsWithRadius) {
    double pitch = cg::linkPitch(1.2);
    EXPECT_GT(cg::sprocketTeeth(20.0, pitch), cg::sprocketTeeth(10.0, pitch));
}

TEST(ChainGeometry, DriveSprocketKeepsLargeVisualSize) {
    for (double wt : {4.0, 8.0, 14.0, 24.0}) {
        double r = cg::linkRadius(wt);
        double half = cg::chainHalfWidth(wt);
        double nodeR = cg::sprocketPitchRadius(half, r);
        double driveR = cg::driveSprocketPitchRadius(half, r);

        EXPECT_GE(driveR, nodeR * 2.8) << "wt=" << wt;
        EXPECT_GE(driveR, half * 3.0) << "wt=" << wt;
        EXPECT_GE(driveR, 15.0) << "wt=" << wt;
    }
}

TEST(ChainGeometry, SourceDrivePathUsesTrueTangents) {
    double wt = 8.0;
    double rollerR = cg::linkRadius(wt);
    double nodeR = cg::sprocketPitchRadius(cg::chainHalfWidth(wt), rollerR);
    double driveR = cg::driveSprocketPitchRadius(cg::chainHalfWidth(wt), rollerR);
    auto path = cg::sourceDrivePath(Vec2(0, 0), Vec2(260, 0), nodeR, driveR);
    ASSERT_TRUE(path.valid);

    expectTangent(Vec2(0, 0), path.aTop, path.driveLeftTop);
    expectTangent(path.center, path.driveLeftTop, path.aTop);
    expectTangent(path.center, path.driveRightTop, path.bTop);
    expectTangent(Vec2(260, 0), path.bTop, path.driveRightTop);
    expectTangent(path.center, path.driveRightBottom, path.bBottom);
    expectTangent(path.center, path.driveLeftBottom, path.aBottom);

    double topContact =
        cg::clockwiseDelta(cg::angleOf(path.driveLeftTop - path.center),
                           cg::angleOf(path.driveRightTop - path.center));
    double bottomContact =
        cg::clockwiseDelta(cg::angleOf(path.driveRightBottom - path.center),
                           cg::angleOf(path.driveLeftBottom - path.center));
    EXPECT_LT(topContact + bottomContact, cg::kPi * 0.35);
}

TEST(ChainGeometry, SourceDrivePhaseRunsOppositePositiveChainTravel) {
    EXPECT_LT(cg::sourceDriveSprocketPhaseFromChainTravel(10.0, 5.0), 0.0);
    EXPECT_GT(cg::sourceDriveSprocketPhaseFromChainTravel(-10.0, 5.0), 0.0);
    EXPECT_DOUBLE_EQ(cg::sourceDriveSprocketPhaseFromChainTravel(10.0, 0.0), 0.0);
}

// --- the regression: simulated chain stays on the sprocket pitch circle -------

TEST(ChainSimEngagement, LinksRideThePitchTrack) {
    const double wt = 10.0;
    const Vec2 a(0, 0), b(260, 0);
    ChainSim sim;
    sim.configure({wireSpec(a, b, wt, 40.0)}, cg::linkRadius(wt));
    for (int i = 0; i < 180; ++i)
        sim.step(1.0 / 60.0);

    double rollerR = cg::linkRadius(wt);
    double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(wt), rollerR);
    auto links = sim.links();
    ASSERT_GE(links.size(), 8u);

    int onArcs = 0;
    for (const auto& link : links) {
        EXPECT_LT(pitchTrackDeviation(link.pos, a, b, pitchR), rollerR * 1.6)
            << "link " << link.indexInLoop << " left the pitch track";
        double along = link.pos.x; // axis is +x
        if (along < 0.0 || along > (b - a).length()) ++onArcs;
    }
    // The wrap around the sprockets is actually exercised, not vacuous.
    EXPECT_GE(onArcs, 2);
}

TEST(ChainSimEngagement, ArcRadiusEqualsRenderedGearPitchRadius) {
    // The exact engagement contract: links beyond a segment end keep distance
    // ~pitchR from the node — the same pitchR the gear renderer uses.
    const double wt = 8.0;
    const Vec2 a(0, 0), b(220, 0);
    ChainSim sim;
    sim.configure({wireSpec(a, b, wt, 30.0)}, cg::linkRadius(wt));
    for (int i = 0; i < 120; ++i)
        sim.step(1.0 / 60.0);

    double rollerR = cg::linkRadius(wt);
    double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(wt), rollerR);
    int checked = 0;
    for (const auto& link : sim.links()) {
        Vec2 node;
        if (link.pos.x < 0.0) node = a;
        else if (link.pos.x > b.x) node = b;
        else continue;
        ++checked;
        EXPECT_NEAR((link.pos - node).length(), pitchR, rollerR * 1.6);
    }
    EXPECT_GE(checked, 1);
}

TEST(ChainSimEngagement, VoltageSourceLinksTouchOnlyShortDriveArc) {
    const double wt = 8.0;
    const Vec2 a(0, 0), b(260, 0);
    ChainSim sim;
    sim.configure({sourceSpec(a, b, wt, 35.0)}, cg::linkRadius(wt));
    for (int i = 0; i < 90; ++i)
        sim.step(1.0 / 60.0);

    double rollerR = cg::linkRadius(wt);
    double pitchR = cg::driveSprocketPitchRadius(cg::chainHalfWidth(wt), rollerR);
    Vec2 center = (a + b) * 0.5;

    int seated = 0;
    for (const auto& link : sim.links()) {
        double err = std::abs((link.pos - center).length() - pitchR);
        if (err < rollerR * 0.35)
            ++seated;
    }

    EXPECT_GE(seated, 1) << "source drive gear has no physical chain contact";
    EXPECT_LE(seated, 6) << "source chain is stuck around too much of the gear";
}

// --- rendered sprocket comes from the same geometry ---------------------------

TEST(MechanicsGears, SprocketUsesSharedGeometryAtJunctions) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 150));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    int n3 = c.addNode(Vec2(200, 150));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
    c.addComponent(ComponentType::Wire, n2, n3, 0.0);

    CircuitSolver solver;
    CircuitSolution solution = solver.solve(c);
    ViewParams p;
    ProjectionResult res = buildProjection(ProjectionKind::Mechanical, c, &solution, p);

    double rollerR = cg::linkRadius(p.wireThickness);
    double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(p.wireThickness), rollerR);
    double rootR = cg::sprocketRootRadius(pitchR, rollerR);
    double tipR = cg::sprocketTipRadius(pitchR, rollerR);
    int teeth = cg::sprocketTeeth(pitchR, cg::linkPitch(rollerR));
    Vec2 junction(200, 0); // n2: resistor + wire meet here

    // Disc body: a filled circle of exactly the root radius at the junction.
    int bodies = 0;
    for (const auto& circle : res.prims.circles)
        if (circle.filled && std::abs(circle.radius - rootR) < 1e-9 &&
            (circle.center - junction).length() < 1e-9)
            ++bodies;
    EXPECT_EQ(bodies, 1);

    // Teeth: exactly `teeth` filled quads around the junction, every corner
    // inside the tip circle and outside ~the root circle.
    int toothQuads = 0;
    for (const auto& quad : res.prims.quads) {
        Vec2 centroid((quad.p1.x + quad.p2.x + quad.p3.x + quad.p4.x) * 0.25,
                      (quad.p1.y + quad.p2.y + quad.p3.y + quad.p4.y) * 0.25);
        if ((centroid - junction).length() > tipR) continue;
        if (!quad.filled) continue;
        ++toothQuads;
        for (Vec2 corner : {quad.p1, quad.p2, quad.p3, quad.p4}) {
            double d = (corner - junction).length();
            EXPECT_LE(d, tipR + 1e-9);
            EXPECT_GE(d, rootR - 1e-9);
        }
    }
    EXPECT_EQ(toothQuads, teeth);
}

// --- rendered chain is a bicycle chain over the sim links ---------------------

TEST(MechanicsChain, BicycleChainIsBuiltFromSimLinks) {
    Circuit c;
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(260, 0));
    int wireId = c.addComponent(ComponentType::Wire, n1, n2, 0.0);

    ViewParams p;
    double rollerR = cg::linkRadius(p.wireThickness);

    ChainSim sim;
    ChainSpec spec = wireSpec(Vec2(0, 0), Vec2(260, 0), p.wireThickness, 30.0);
    spec.componentId = wireId;
    sim.configure({spec}, rollerR);
    for (int i = 0; i < 60; ++i)
        sim.step(1.0 / 60.0);
    std::vector<ChainLink> links = sim.links();
    ASSERT_GE(links.size(), 8u);
    const int n = static_cast<int>(links.size());

    p.chainLinks = &links;
    ProjectionResult res = buildProjection(ProjectionKind::Mechanical, c, nullptr, p);

    // Rollers: one filled circle of the link radius per sim link, at its pos.
    int rollers = 0, pins = 0;
    for (const auto& circle : res.prims.circles) {
        if (!circle.filled) continue;
        if (std::abs(circle.radius - rollerR) < 1e-9) ++rollers;
        if (std::abs(circle.radius - rollerR * 0.35) < 1e-9) ++pins;
    }
    EXPECT_EQ(rollers, n);
    EXPECT_EQ(pins, n);

    // Plates: every consecutive pair (closed ring) joined by a PAIR of plate
    // lines; outer and inner plates alternate, both kinds present.
    int outer = 0, inner = 0;
    for (const auto& line : res.prims.lines) {
        if (std::abs(line.width - cg::kOuterPlateWidth) < 1e-9) ++outer;
        if (std::abs(line.width - cg::kInnerPlateWidth) < 1e-9) ++inner;
    }
    EXPECT_EQ(outer + inner, 2 * n);
    EXPECT_GE(outer, 2);
    EXPECT_GE(inner, 2);
}

TEST(MechanicsChain, VoltageSourceDrawsLargeDriveSprocketUnderTangentLinks) {
    Circuit c;
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(260, 0));
    int sourceId = c.addComponent(ComponentType::VoltageSource, n1, n2, 5.0);

    ViewParams p;
    double rollerR = cg::linkRadius(p.wireThickness);
    double half = cg::chainHalfWidth(p.wireThickness);
    double pitchR = cg::driveSprocketPitchRadius(half, rollerR);
    double rootR = cg::sprocketRootRadius(pitchR, rollerR);
    double tipR = cg::sprocketTipRadius(pitchR, rollerR);
    int teeth = cg::sprocketTeeth(pitchR, cg::linkPitch(rollerR));
    Vec2 center(130, 0);

    ChainSim sim;
    ChainSpec spec = sourceSpec(Vec2(0, 0), Vec2(260, 0), p.wireThickness, 30.0);
    spec.componentId = sourceId;
    sim.configure({spec}, rollerR);
    for (int i = 0; i < 60; ++i)
        sim.step(1.0 / 60.0);
    std::vector<ChainLink> links = sim.links();
    ASSERT_GE(links.size(), 8u);

    p.chainLinks = &links;
    ProjectionResult res = buildProjection(ProjectionKind::Mechanical, c, nullptr, p);

    int bodies = 0;
    for (const auto& circle : res.prims.circles)
        if (circle.filled && std::abs(circle.radius - rootR) < 1e-9 &&
            (circle.center - center).length() < 1e-9)
            ++bodies;
    EXPECT_EQ(bodies, 1);

    int toothQuads = 0;
    for (const auto& quad : res.prims.quads) {
        Vec2 centroid((quad.p1.x + quad.p2.x + quad.p3.x + quad.p4.x) * 0.25,
                      (quad.p1.y + quad.p2.y + quad.p3.y + quad.p4.y) * 0.25);
        if ((centroid - center).length() > tipR) continue;
        if (quad.filled)
            ++toothQuads;
    }
    EXPECT_EQ(toothQuads, teeth);

    int seatedRollers = 0;
    for (const auto& circle : res.prims.circles) {
        if (!circle.filled || std::abs(circle.radius - rollerR) > 1e-9)
            continue;
        double err = std::abs((circle.center - center).length() - pitchR);
        if (err < rollerR * 0.35)
            ++seatedRollers;
    }
    EXPECT_GE(seatedRollers, 1);
    EXPECT_LE(seatedRollers, 6);
}

TEST(MechanicsChain, SourceDriveSprocketTurnsWithChainNotAgainstIt) {
    Circuit c;
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(260, 0));
    int sourceId = c.addComponent(ComponentType::VoltageSource, n1, n2, 5.0);

    CircuitSolution solution;
    solution.nodePotentials.push_back({n1, 5.0});
    solution.nodePotentials.push_back({n2, 0.0});
    solution.branches.push_back({sourceId, 0.25, 5.0, -1.25});

    ViewParams p;
    double rollerR = cg::linkRadius(p.wireThickness);
    double pitchR = cg::driveSprocketPitchRadius(cg::chainHalfWidth(p.wireThickness), rollerR);
    double rootR = cg::sprocketRootRadius(pitchR, rollerR);
    double tipR = cg::sprocketTipRadius(pitchR, rollerR);
    Vec2 center(130, 0);

    FlowIntegrals beforeFlow;
    beforeFlow.component[sourceId] = 0.0;
    p.flowIntegrals = &beforeFlow;
    ProjectionResult before = buildProjection(ProjectionKind::Mechanical, c, &solution, p);

    FlowIntegrals afterFlow;
    afterFlow.component[sourceId] = 0.2;
    p.flowIntegrals = &afterFlow;
    ProjectionResult after = buildProjection(ProjectionKind::Mechanical, c, &solution, p);

    double beforeAngle = firstSprocketToothAngle(before, center, rootR, tipR);
    double afterAngle = firstSprocketToothAngle(after, center, rootR, tipR);
    ASSERT_TRUE(std::isfinite(beforeAngle));
    ASSERT_TRUE(std::isfinite(afterAngle));

    EXPECT_LT(signedAngleDelta(beforeAngle, afterAngle), -0.05);
}
