#include <gtest/gtest.h>
#include <cmath>
#include "circuit/Circuit.h"
#include "physics/DriftModel.h"
#include "physics/ParticleSim.h"
#include "projection/HydraulicMapping.h"
#include "projection/ProjectionBuilder.h"
#include "solver/CircuitSolver.h"

using namespace current_lab::hydraulic;
using namespace current_lab::projection;

TEST(HydraulicMapping, FlowIsSignCorrectAndMonotonic) {
    EXPECT_GT(flowFromCurrent(0.2), flowFromCurrent(0.1));
    EXPECT_LT(flowFromCurrent(-0.1), 0.0);
    EXPECT_DOUBLE_EQ(flowFromCurrent(0.0), 0.0);
}

TEST(HydraulicMapping, HydraulicPowerEqualsElectricalPower) {
    for (double v : {-3.0, 0.5, 12.0})
        for (double i : {-0.2, 0.01, 0.3})
            EXPECT_DOUBLE_EQ(hydraulicPower(pressureFromPotential(v), flowFromCurrent(i)), v * i);
}

TEST(HydraulicMapping, TankFillTracksCapVoltageAndClamps) {
    EXPECT_GT(tankFillFraction(4.0, 5.0), tankFillFraction(2.0, 5.0));
    EXPECT_DOUBLE_EQ(tankFillFraction(0.0, 5.0), 0.0);
    EXPECT_DOUBLE_EQ(tankFillFraction(9.0, 5.0), 1.0);  // clamped
    EXPECT_DOUBLE_EQ(tankFillFraction(-2.5, 5.0), 0.5); // magnitude
}

TEST(HydraulicMapping, AnalogEnergiesMatchElectricalEnergies) {
    EXPECT_DOUBLE_EQ(tankEnergy(1e-3, 4.0), current_lab::physics::capacitorEnergy(1e-3, 4.0));
    EXPECT_DOUBLE_EQ(turbineEnergy(2.0, 0.3), current_lab::physics::inductorEnergy(2.0, 0.3));
}

namespace {

Circuit makeRlcCircuit(int& capId) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 200));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    int n3 = c.addNode(Vec2(400, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
    c.addComponent(ComponentType::Inductor, n2, n3, 1.0);
    capId = c.addComponent(ComponentType::Capacitor, n3, gnd, 1e-3);
    return c;
}

ViewParams waterParams() {
    ViewParams p;
    p.layers.potential = true;
    p.layers.heat = true;
    return p;
}

} // namespace

TEST(HydraulicProjection, ElementsMatchPhysicsProjectionExactly) {
    int capId;
    Circuit c = makeRlcCircuit(capId);
    CircuitSolver solver;
    auto solution = solver.solve(c);

    auto water = buildProjection(ProjectionKind::Hydraulic, c, &solution, waterParams());
    auto physics = buildProjection(ProjectionKind::Physics, c, &solution, waterParams());

    ASSERT_EQ(water.elements.size(), physics.elements.size());
    for (size_t i = 0; i < water.elements.size(); ++i) {
        EXPECT_EQ(water.elements[i].componentId, physics.elements[i].componentId);
        EXPECT_DOUBLE_EQ(water.elements[i].current, physics.elements[i].current);
        EXPECT_DOUBLE_EQ(water.elements[i].storedEnergy, physics.elements[i].storedEnergy);
    }
}

TEST(HydraulicProjection, TankLevelRisesAsCapacitorCharges) {
    int capId;
    Circuit c = makeRlcCircuit(capId);
    CircuitSolver solver;

    TransientState empty;
    auto unchargedSolution = solver.solveTransientSnapshot(c, empty);
    auto uncharged = buildProjection(ProjectionKind::Hydraulic, c, &unchargedSolution, waterParams());

    TransientState charged;
    charged.capVoltage[capId] = 5.0;
    auto chargedSolution = solver.solveTransientSnapshot(c, charged);
    auto full = buildProjection(ProjectionKind::Hydraulic, c, &chargedSolution, waterParams());

    // A charged tank adds the water-fill quad and the level line.
    EXPECT_GT(full.prims.quads.size(), uncharged.prims.quads.size());
}

TEST(HydraulicProjection, WaterFlowFollowsCurrentSign) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(300, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    int src = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
    c.addComponent(ComponentType::Wire, n2, gnd, 0.0);

    CircuitSolver solver;
    auto solution = solver.solve(c);
    ViewParams params = waterParams();
    params.time = 0.01;
    auto forward = buildProjection(ProjectionKind::Hydraulic, c, &solution, params);

    c.findComponent(src)->value = -5.0;
    auto reversedSolution = solver.solve(c);
    auto reversed = buildProjection(ProjectionKind::Hydraulic, c, &reversedSolution, params);

    ASSERT_FALSE(forward.prims.particles.empty());
    ASSERT_EQ(forward.prims.particles.size(), reversed.prims.particles.size());
    bool anyDifferent = false;
    for (size_t i = 0; i < forward.prims.particles.size(); ++i) {
        if ((forward.prims.particles[i].pos - reversed.prims.particles[i].pos).length() > 1e-9) {
            anyDifferent = true;
            break;
        }
    }
    EXPECT_TRUE(anyDifferent);
}

TEST(HydraulicProjection, GateValveShowsOpenAndClosedStates) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    int swId = c.addComponent(ComponentType::Switch, n1, n2, 1.0);
    c.addComponent(ComponentType::Resistor, n2, gnd, 100.0);

    CircuitSolver solver;
    auto closedSolution = solver.solve(c);
    auto closed = buildProjection(ProjectionKind::Hydraulic, c, &closedSolution, waterParams());

    c.findComponent(swId)->value = 0.0;
    auto openSolution = solver.solve(c);
    auto open = buildProjection(ProjectionKind::Hydraulic, c, &openSolution, waterParams());

    // Open valve splits the pipe -> more pipe shells; closed passes flow.
    EXPECT_GT(open.prims.quads.size(), closed.prims.quads.size());
    EXPECT_GT(closed.prims.particles.size(), open.prims.particles.size());
}

TEST(HydraulicProjection, WaterBallsAreDrawnAtTheColliderSize) {
    // Regression (user, 2026-06-11): water balls looked squashed half a radius
    // into each other. The renderer inflated them 1.25x over the Box2D
    // collider, so TOUCHING colliders (centres 2r apart) were drawn
    // overlapping. The drawn radius must BE the collider radius the water
    // world is configured with: particleWorldRadius(wireThickness).
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(300, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    int resId = c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
    c.addComponent(ComponentType::Wire, n2, gnd, 0.0);

    CircuitSolver solver;
    auto solution = solver.solve(c);
    ViewParams params = waterParams();
    params.time = 0.01;

    double r = current_lab::physics::particleWorldRadius(params.wireThickness);
    std::vector<current_lab::physics::SimParticle> touching(2);
    touching[0].id = 1;
    touching[0].pos = Vec2(150.0 - r, 0.0); // a touching pair on the resistor
    touching[0].componentId = resId;
    touching[1].id = 2;
    touching[1].pos = Vec2(150.0 + r, 0.0);
    touching[1].componentId = resId;
    params.simParticles = &touching;

    auto res = buildProjection(ProjectionKind::Hydraulic, c, &solution, params);

    int found = 0;
    for (const auto& prim : res.prims.particles) {
        for (const auto& sp : touching) {
            if ((prim.pos - sp.pos).length() > 1e-9) continue;
            ++found;
            EXPECT_FALSE(prim.screenSpaceRadius);
            // Drawn circles of touching colliders may meet, never overlap.
            EXPECT_NEAR(prim.radius, r, 1e-9)
                << "water ball drawn " << prim.radius / r
                << "x its physical size";
        }
    }
    EXPECT_EQ(found, 2);
}
