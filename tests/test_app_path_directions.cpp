#include <gtest/gtest.h>
#include <cmath>
#include <map>
#include "circuit/Circuit.h"
#include "physics/ChannelSpecs.h"
#include "projection/ProjectionBuilder.h"
#include "solver/CircuitSolver.h"

// THE APP PATH for directions (user report 2026-06-11: «нижний проводник и
// проводник с источником текут в одну точку — левый нижний угол»):
// circuit -> toDistributed -> solve -> map back (exact copy of
// MainWindow::mapDistributedSolution) -> current arrows / drift / water.
// These tests pin the contract: conventional current circulates consistently
// around the loop THROUGH the source (into the bottom-left node along the
// bottom wire, OUT of it up through the source).
namespace {

using namespace current_lab::physics;
using namespace current_lab::projection;

Circuit makeAppLoop(int& srcId, int& resId, int& wireRightId, int& wireBottomId) {
    // Same shape as MainWindow::setupTestCircuit: source left, R top,
    // ground = bottom-left node.
    Circuit c;
    int gnd = c.addNode(Vec2(200, 300), "GND");
    int n1 = c.addNode(Vec2(200, 150), "N1");
    int n2 = c.addNode(Vec2(450, 150), "N2");
    c.groundNodeId = gnd;
    int corner = c.addNode(Vec2(450, 300));
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    srcId = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resId = c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    wireRightId = c.addComponent(ComponentType::Wire, n2, corner, 0.0);
    wireBottomId = c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
    return c;
}

// Exact copy of MainWindow::mapDistributedSolution (kept in sync by THIS test
// being next to it; if you change one, change both).
CircuitSolution mapBack(const Circuit& original, const Circuit& distributed,
                        const CircuitSolution& distSol) {
    CircuitSolution out;
    auto potentialForNode = [&](int nodeId) {
        for (const auto& np : distSol.nodePotentials)
            if (np.nodeId == nodeId) return np.potential;
        return 0.0;
    };
    for (const auto& node : original.nodes)
        out.nodePotentials.push_back({node.id, potentialForNode(node.id)});

    // Match by componentId (solver skips Ground in branches, so positions
    // do NOT line up with distributedSource) — same as the fixed
    // MainWindow::mapDistributedSolution.
    auto originalIdFor = [&](int distributedComponentId) {
        int idx = distributed.componentIndex(distributedComponentId);
        if (idx < 0 || idx >= (int)distributed.distributedSource.size()) return -1;
        return distributed.distributedSource[idx];
    };

    for (const auto& oc : original.components) {
        if (oc.type == ComponentType::Ground) continue;
        BranchResult br;
        br.componentId = oc.id;
        bool isWire = (oc.type == ComponentType::Wire);
        double totalCurrent = 0.0, totalVdrop = 0.0, totalPower = 0.0;
        int segCount = 0;
        for (const auto& db : distSol.branches) {
            if (originalIdFor(db.componentId) != oc.id) continue;
            if (isWire) {
                totalCurrent += db.current;
                totalVdrop += db.voltageDrop;
                totalPower += db.power;
                segCount++;
            } else {
                br.current = db.current;
                br.voltageDrop = db.voltageDrop;
                br.power = db.power;
                break;
            }
        }
        if (isWire) {
            br.current = segCount > 0 ? totalCurrent / segCount : 0.0;
            br.voltageDrop = totalVdrop;
            br.power = totalPower;
        }
        out.branches.push_back(br);
    }
    return out;
}

struct AppSolution {
    Circuit circuit;
    CircuitSolution mapped;
    std::map<int, double> current; // componentId -> mapped branch current
    int srcId, resId, wireRightId, wireBottomId;
};

AppSolution solveAppPath() {
    AppSolution app;
    app.circuit = makeAppLoop(app.srcId, app.resId, app.wireRightId, app.wireBottomId);
    DistributedWireParameters params; // app defaults: 8 segments, 0.5 Ohm/unit
    Circuit distributed = app.circuit.toDistributed(params);
    CircuitSolver solver;
    CircuitSolution distSol = solver.solve(distributed);
    app.mapped = mapBack(app.circuit, distributed, distSol);
    for (const auto& br : app.mapped.branches)
        app.current[br.componentId] = br.current;
    return app;
}

} // namespace

TEST(AppPathDirections, MappedCurrentsObeyKclAtEveryNode) {
    AppSolution app = solveAppPath();
    // Signed branch current flows nodeA -> nodeB. At every node the inflow
    // must equal the outflow — two branches may NOT both flow into a corner.
    for (const auto& node : app.circuit.nodes) {
        double net = 0.0;
        for (const auto& comp : app.circuit.components) {
            if (comp.type == ComponentType::Ground) continue;
            if (comp.nodeB == node.id) net += app.current[comp.id];
            if (comp.nodeA == node.id) net -= app.current[comp.id];
        }
        EXPECT_NEAR(net, 0.0, 1e-9) << "KCL violated at node " << node.id;
    }
}

TEST(AppPathDirections, ConventionalCurrentCirculatesThroughTheSource) {
    AppSolution app = solveAppPath();
    double loopI = app.current[app.resId];
    ASSERT_GT(loopI, 1e-6); // V+ at n1 drives n1 -> n2 through the resistor

    // Bottom wire (corner -> gnd): flows INTO the bottom-left node...
    EXPECT_GT(app.current[app.wireBottomId], 0.0);
    // ...and the source (n1 -> gnd axis) carries it back UP: its branch
    // current is NEGATIVE along n1->gnd, i.e. the flow is gnd -> n1, AWAY
    // from the bottom-left corner. Both flowing into the corner = the bug
    // the user described; this pins the correct signs.
    EXPECT_LT(app.current[app.srcId], 0.0);
    EXPECT_NEAR(app.current[app.srcId], -loopI, loopI * 0.05);
}

TEST(AppPathDirections, CurrentArrowsFollowOneCirculation) {
    AppSolution app = solveAppPath();
    ViewParams p;
    p.layers.current = true;
    p.layers.electronFlow = false; // conventional arrows
    ProjectionResult res = buildProjection(ProjectionKind::Physics, app.circuit,
                                           &app.mapped, p);

    // Mean arrow direction per conductor, projected on the conductor axis.
    auto meanArrowAlong = [&](int compId) {
        const Component* comp = app.circuit.findComponent(compId);
        Vec2 a = app.circuit.findNode(comp->nodeA)->position;
        Vec2 b = app.circuit.findNode(comp->nodeB)->position;
        Vec2 unit = (b - a).normalized();
        Vec2 perp(-unit.y, unit.x);
        double len = (b - a).length();
        double sum = 0.0;
        int n = 0;
        for (const auto& arrow : res.prims.arrows) {
            Vec2 rel = arrow.pos - a;
            double t = rel.x * unit.x + rel.y * unit.y;
            double lat = std::abs(rel.x * perp.x + rel.y * perp.y);
            if (t < -1.0 || t > len + 1.0 || lat > 6.0) continue;
            sum += arrow.dir.x * unit.x + arrow.dir.y * unit.y;
            ++n;
        }
        return n > 0 ? sum / n : 0.0;
    };

    // Wires and resistor: arrows along +axis (current positive a->b).
    EXPECT_GT(meanArrowAlong(app.resId), 0.5);
    EXPECT_GT(meanArrowAlong(app.wireRightId), 0.5);
    EXPECT_GT(meanArrowAlong(app.wireBottomId), 0.5);
    // Source leads: arrows along -axis (gnd -> n1, branch current negative).
    EXPECT_LT(meanArrowAlong(app.srcId), -0.5);
}

TEST(AppPathDirections, CurrentArrowsMarchWhereTheyPoint) {
    // Regression (user, 2026-06-11): source arrows POINTED up the branch but
    // MARCHED down — the glyph direction honoured the current sign, the
    // animation phase did not. Invariant: every arrow that moves between two
    // close frames moves along its own glyph direction. Holds in both the
    // conventional-current and electron-flow modes.
    AppSolution app = solveAppPath();
    for (bool electrons : {false, true}) {
        ViewParams p0;
        p0.layers.current = true;
        p0.layers.electronFlow = electrons;
        p0.time = 1.0;
        ViewParams p1 = p0;
        p1.time = 1.0 + 0.01; // marches <= ~0.1 px, far below arrow spacing

        ProjectionResult f0 = buildProjection(ProjectionKind::Physics, app.circuit,
                                              &app.mapped, p0);
        ProjectionResult f1 = buildProjection(ProjectionKind::Physics, app.circuit,
                                              &app.mapped, p1);
        ASSERT_FALSE(f0.prims.arrows.empty());

        int marching = 0;
        for (const auto& a0 : f0.prims.arrows) {
            // Nearest arrow in the next frame is this arrow a tick later
            // (the step is tiny compared to the spacing between arrows).
            double bestD2 = 1e300;
            Vec2 bestPos = a0.pos;
            for (const auto& a1 : f1.prims.arrows) {
                Vec2 d = a1.pos - a0.pos;
                double d2 = d.x * d.x + d.y * d.y;
                if (d2 < bestD2) { bestD2 = d2; bestPos = a1.pos; }
            }
            Vec2 delta = bestPos - a0.pos;
            double dist = delta.length();
            if (dist < 1e-9 || dist > 2.0) continue; // static glyph or wrapped
            double along = delta.x * a0.dir.x + delta.y * a0.dir.y;
            EXPECT_GT(along, 0.0)
                << (electrons ? "electron" : "conventional") << " arrow at ("
                << a0.pos.x << ", " << a0.pos.y << ") points ("
                << a0.dir.x << ", " << a0.dir.y << ") but moved ("
                << delta.x << ", " << delta.y << ")";
            ++marching;
        }
        EXPECT_GT(marching, 10) << "animation produced no moving arrows";
    }
}

TEST(AppPathDirections, DriftAndWaterCirculateConsistentlyFromMappedSolution) {
    AppSolution app = solveAppPath();
    for (bool waterWorld : {false, true}) {
        auto specs = makeChannelSpecs(app.circuit, &app.mapped, 8.0, waterWorld);
        for (const auto& spec : specs) {
            double branchI = app.current[spec.componentId];
            if (std::abs(branchI) < 1e-9) continue;
            // Water flows WITH conventional current, electrons AGAINST it —
            // each world must be consistent with the mapped solution sign.
            double expectedSign = waterWorld ? branchI : -branchI;
            EXPECT_GT(spec.targetSpeed * expectedSign, 0.0)
                << (waterWorld ? "water" : "electrons") << " comp "
                << spec.componentId;
        }
    }
}
