#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "circuit/Circuit.h"
#include "physics/ChannelSpecs.h"
#include "physics/DriftModel.h"
#include "physics/ParticleSim.h"
#include "physics/ResistiveElementModel.h"
#include "projection/ProjectionBuilder.h"
#include "solver/CircuitSolver.h"

// Regression (user, 2026-06-12): «схема с одним резистором — в проводнике, в
// котором стоит резистор, не отображается поток электронов». The Physics pane
// renders the Box2D electron world (ViewParams::simParticles); the sim tags
// every particle with the REAL component id (makeChannelSpecs builds one
// channel per component), while the resistor branch filtered the sim output
// with per-section pseudo-ids (comp.id * 17 + si) — no particle ever matched
// and the whole resistor conductor rendered empty while plain wires flowed.
namespace {

using namespace current_lab::physics;
using namespace current_lab::projection;

// Single-resistor loop, same shape as MainWindow::setupTestCircuit:
// source on the left, resistor on top, wires close the loop.
Circuit makeSingleResistorLoop(int& srcId, int& resId, int& wireRightId,
                               int& wireBottomId) {
    Circuit c;
    int gnd = c.addNode(Vec2(200, 300), "GND");
    int n1 = c.addNode(Vec2(200, 150), "N1");
    int n2 = c.addNode(Vec2(450, 150), "N2");
    int corner = c.addNode(Vec2(450, 300));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    srcId = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resId = c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    wireRightId = c.addComponent(ComponentType::Wire, n2, corner, 0.0);
    wireBottomId = c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
    return c;
}

ViewParams driftParams() {
    ViewParams p;
    p.layers.drift = true;
    return p;
}

double distToSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    double len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 <= 1e-12) return (p - a).length();
    double t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2;
    t = std::clamp(t, 0.0, 1.0);
    return (p - (a + ab * t)).length();
}

int renderedNear(const ProjectionResult& res, Vec2 a, Vec2 b, double maxDist) {
    int n = 0;
    for (const auto& prt : res.prims.particles)
        if (distToSegment(prt.pos, a, b) <= maxDist) ++n;
    return n;
}

bool renderedAt(const ProjectionResult& res, Vec2 pos, double tol = 1e-9) {
    for (const auto& prt : res.prims.particles)
        if ((prt.pos - pos).length() <= tol) return true;
    return false;
}

int renderedCountAt(const ProjectionResult& res, Vec2 pos, double tol = 1e-9) {
    int n = 0;
    for (const auto& prt : res.prims.particles)
        if ((prt.pos - pos).length() <= tol) ++n;
    return n;
}

} // namespace

// Contract between the Box2D world and the renderer: a sim particle tagged
// with ANY channel's componentId must be drawn by the Physics projection.
// Pre-fix this failed exactly for the resistor channel.
TEST(ResistorDrift, EverySimChannelComponentIdIsRendered) {
    int srcId, resId, wireRightId, wireBottomId;
    Circuit circuit = makeSingleResistorLoop(srcId, resId, wireRightId, wireBottomId);
    CircuitSolver solver;
    CircuitSolution solution = solver.solve(circuit);

    auto specs = makeChannelSpecs(circuit, &solution, 8.0, /*waterWorld=*/false);
    ASSERT_FALSE(specs.empty());

    // One synthetic particle per channel, parked at the channel midpoint and
    // tagged exactly the way ParticleSim tags its output.
    std::vector<SimParticle> simParticles;
    for (const auto& spec : specs) {
        SimParticle sp;
        sp.id = static_cast<uint64_t>(simParticles.size() + 1);
        sp.pos = (spec.a + spec.b) * 0.5;
        sp.componentId = spec.componentId;
        simParticles.push_back(sp);
    }

    ViewParams p = driftParams();
    p.simParticles = &simParticles;
    ProjectionResult res = buildProjection(ProjectionKind::Physics, circuit,
                                           &solution, p);

    for (size_t i = 0; i < specs.size(); ++i) {
        EXPECT_TRUE(renderedAt(res, simParticles[i].pos))
            << "sim particle of component " << specs[i].componentId
            << (specs[i].componentId == resId ? " (THE RESISTOR)" : "")
            << " was not rendered by the Physics projection";
    }
}

// End-to-end through the real Box2D electron world: the resistor conductor
// must show flowing electrons, drawn exactly once per sim particle and at the
// exact sim position (the drawn ball IS the collider).
TEST(ResistorDrift, Box2DElectronsVisibleOnResistorConductor) {
    int srcId, resId, wireRightId, wireBottomId;
    Circuit circuit = makeSingleResistorLoop(srcId, resId, wireRightId, wireBottomId);
    CircuitSolver solver;
    CircuitSolution solution = solver.solve(circuit);

    const double wireThickness = 8.0;
    auto specs = makeChannelSpecs(circuit, &solution, wireThickness,
                                  /*waterWorld=*/false);
    ParticleSim sim;
    sim.configure(specs, particleWorldRadius(wireThickness));
    for (int i = 0; i < 30; ++i) sim.step(1.0 / 60.0);
    std::vector<SimParticle> simParticles = sim.particles();

    // The sim itself must populate the resistor channel with REAL-id particles.
    int simOnResistor = 0;
    for (const auto& sp : simParticles)
        if (sp.componentId == resId) ++simOnResistor;
    ASSERT_GT(simOnResistor, 0) << "Box2D world seeded no particles for the resistor";

    ViewParams p = driftParams();
    p.wireThickness = wireThickness;
    p.simParticles = &simParticles;
    ProjectionResult res = buildProjection(ProjectionKind::Physics, circuit,
                                           &solution, p);

    // Every resistor-channel particle is drawn at its exact sim position...
    int matched = 0;
    for (const auto& sp : simParticles) {
        if (sp.componentId != resId) continue;
        if (renderedAt(res, sp.pos, 1e-9)) ++matched;
    }
    EXPECT_EQ(matched, simOnResistor)
        << "resistor sim particles missing from the rendered Physics view";

    // ...exactly once: away from the junctions no other channel overlaps the
    // resistor, so the rendered count there equals the sim count there.
    Vec2 a = circuit.findNode(circuit.findComponent(resId)->nodeA)->position;
    Vec2 b = circuit.findNode(circuit.findComponent(resId)->nodeB)->position;
    Vec2 unit = (b - a).normalized();
    double len = (b - a).length();
    Vec2 ta = a + unit * 20.0;
    Vec2 tb = a + unit * (len - 20.0);
    double lane = wireThickness; // half-width + radius + slack
    int simTrimmed = 0;
    for (const auto& sp : simParticles)
        if (sp.componentId == resId && distToSegment(sp.pos, ta, tb) <= lane)
            ++simTrimmed;
    ASSERT_GT(simTrimmed, 0);
    EXPECT_EQ(renderedNear(res, ta, tb, lane), simTrimmed)
        << "rendered electron count over the resistor body must match the sim "
           "(no duplicates, no aliens)";

    // The drawn ball is the collider: radius matches the sim configuration.
    for (const auto& prt : res.prims.particles) {
        if (distToSegment(prt.pos, ta, tb) > lane) continue;
        EXPECT_NEAR(prt.radius, particleWorldRadius(wireThickness), 1e-9);
        EXPECT_FALSE(prt.screenSpaceRadius);
    }
}

// The Drude pillar lattice in the sim lives exactly under the DRAWN resistive
// body: an invisible pillar under a section drawn as a plain lead reads as
// electrons bouncing off nothing (review finding, 2026-06-12).
TEST(ResistorDrift, ScattererSpanMatchesDrawnResistorBody) {
    for (double len : {60.0, 150.0, 250.0, 400.0}) {
        for (double t : {4.0, 8.0, 16.0}) {
            AxialSpan span = resistorBodySpan(len, t);
            auto sections = resistorPathSections(Vec2(0, 0), Vec2(len, 0), 1.0, 0.0, t);
            const ConductivePathSection* body = nullptr;
            for (const auto& s : sections)
                if (s.material == VisualMaterial::ResistiveBody) body = &s;
            ASSERT_NE(body, nullptr) << "len=" << len << " t=" << t;
            EXPECT_NEAR(span.start, body->start.x, 1e-9) << "len=" << len << " t=" << t;
            EXPECT_NEAR(span.end, body->end.x, 1e-9) << "len=" << len << " t=" << t;
        }
    }
}

// Pseudo-ids (comp.id * 17 + si) must never reach the sim-particle filter:
// here a real wire is GIVEN the colliding id resId*17+1, and with the Box2D
// path active every sim particle must still render exactly once — a reverted
// gate would pull the wire's particles a second time through the resistor's
// section loop.
TEST(ResistorDrift, PseudoIdCollisionDoesNotDuplicateParticles) {
    Circuit c;
    int gnd = c.addNode(Vec2(200, 300), "GND");
    int n1 = c.addNode(Vec2(200, 150), "N1");
    int n2 = c.addNode(Vec2(450, 150), "N2");
    int corner = c.addNode(Vec2(450, 300));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    int resId = c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    c.addComponent(ComponentType::Wire, n2, corner, 0.0);
    int collidingId = resId * 17 + 1;
    c.addComponentWithId(collidingId, ComponentType::Wire, corner, gnd, 0.0);

    CircuitSolver solver;
    CircuitSolution solution = solver.solve(c);

    auto specs = makeChannelSpecs(c, &solution, 8.0, /*waterWorld=*/false);
    std::vector<SimParticle> simParticles;
    for (const auto& spec : specs) {
        SimParticle sp;
        sp.id = static_cast<uint64_t>(simParticles.size() + 1);
        sp.pos = (spec.a + spec.b) * 0.5;
        sp.componentId = spec.componentId;
        simParticles.push_back(sp);
    }

    ViewParams p = driftParams();
    p.simParticles = &simParticles;
    ProjectionResult res = buildProjection(ProjectionKind::Physics, c, &solution, p);

    EXPECT_EQ(res.prims.particles.size(), simParticles.size())
        << "every sim particle must render exactly once even when a section "
           "pseudo-id collides with a real component id";
    for (const auto& sp : simParticles)
        EXPECT_EQ(renderedCountAt(res, sp.pos), 1) << "component " << sp.componentId;
}

// Same hazard class for the capacitor leads (pseudo-ids comp.id * 31 + 1/2):
// the Box2D world has no capacitor channel, so with simParticles active the
// leads draw nothing — and a colliding real id must not be pulled twice.
TEST(ResistorDrift, CapacitorLeadsDrawNoSimParticlesEvenOnIdCollision) {
    Circuit c;
    int n1 = c.addNode(Vec2(0, 0), "N1");
    int n2 = c.addNode(Vec2(120, 0), "N2");
    int n3 = c.addNode(Vec2(240, 0), "N3");
    int capId = c.addComponent(ComponentType::Capacitor, n1, n2, 1e-6);
    int collidingId = capId * 31 + 1;
    c.addComponentWithId(collidingId, ComponentType::Wire, n2, n3, 0.0);

    // Hand-built transient snapshot: the capacitor branch carries current.
    CircuitSolution solution;
    solution.nodePotentials = {{n1, 5.0}, {n2, 0.0}, {n3, 0.0}};
    BranchResult capBranch;
    capBranch.componentId = capId;
    capBranch.current = 0.005;
    BranchResult wireBranch;
    wireBranch.componentId = collidingId;
    wireBranch.current = 0.005;
    solution.branches = {capBranch, wireBranch};

    std::vector<SimParticle> simParticles;
    SimParticle sp;
    sp.id = 1;
    sp.pos = Vec2(180, 0); // wire midpoint
    sp.componentId = collidingId;
    simParticles.push_back(sp);

    ViewParams p = driftParams();
    p.simParticles = &simParticles;
    ProjectionResult res = buildProjection(ProjectionKind::Physics, c, &solution, p);

    EXPECT_EQ(renderedCountAt(res, sp.pos), 1)
        << "the colliding wire's particle must render exactly once, not also "
           "through the capacitor's lead pseudo-ids";
    Vec2 a = c.findNode(n1)->position;
    Vec2 b = c.findNode(n2)->position;
    EXPECT_EQ(renderedNear(res, a, b, 6.0), 0)
        << "capacitor leads must stay empty when the Box2D world is active";
}

// The stateless fallback (no Box2D world wired in) keeps the per-section
// rendering: sampled drift covers the leads AND the resistive body.
TEST(ResistorDrift, SamplingFallbackCoversLeadsAndBody) {
    int srcId, resId, wireRightId, wireBottomId;
    Circuit circuit = makeSingleResistorLoop(srcId, resId, wireRightId, wireBottomId);
    CircuitSolver solver;
    CircuitSolution solution = solver.solve(circuit);

    ViewParams p = driftParams(); // simParticles stays null -> sampling path
    ProjectionResult res = buildProjection(ProjectionKind::Physics, circuit,
                                           &solution, p);

    Vec2 a = circuit.findNode(circuit.findComponent(resId)->nodeA)->position;
    Vec2 b = circuit.findNode(circuit.findComponent(resId)->nodeB)->position;
    Vec2 unit = (b - a).normalized();
    double len = (b - a).length();
    double bodyLen = resistorBodyLength(len, p.wireThickness);
    double bodyHalf = resistorBodyHalfWidth(p.wireThickness);
    ASSERT_GT(bodyLen, 0.0);

    Vec2 mid = a + unit * (len * 0.5);
    Vec2 bodyStart = mid - unit * (bodyLen * 0.5);
    Vec2 bodyEnd = mid + unit * (bodyLen * 0.5);

    // Probes are inset from the section boundaries and capped at the section
    // half-width, so they are DISJOINT: a lead particle cannot satisfy the
    // body probe and vice versa (review finding: overlapping probes let a
    // broken body emission pass on lead spill-over).
    double inset = 12.0;
    EXPECT_GT(renderedNear(res, a, bodyStart - unit * inset, p.wireThickness * 0.5), 0)
        << "no sampled drift on the lead before the resistor body";
    EXPECT_GT(renderedNear(res, bodyStart + unit * inset, bodyEnd - unit * inset, bodyHalf), 0)
        << "no sampled drift inside the resistor body";
    EXPECT_GT(renderedNear(res, bodyEnd + unit * inset, b, p.wireThickness * 0.5), 0)
        << "no sampled drift on the lead after the resistor body";
}
