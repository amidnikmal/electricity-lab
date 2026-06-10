#include <gtest/gtest.h>
#include <cmath>
#include "circuit/DemoCircuits.h"
#include "physics/ChannelSpecs.h"
#include "physics/ParticleSim.h"
#include "projection/ProjectionBuilder.h"
#include "solver/CircuitSolver.h"

// Project-wide principle: the views are DIFFERENT worlds and must not leak
// into each other. Electrons are not water, water is not a chain; an EMF
// source has no mechanical obstacles, a pump has an impeller.

using namespace current_lab::physics;
using namespace current_lab::projection;

namespace {

Circuit sourceResistorLoop() {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 150));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(300, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    c.addComponent(ComponentType::Wire, n2, gnd, 0.0);
    return c;
}

} // namespace

TEST(WorldSeparation, ElectronWorldHasNoPaddleInsideTheSource) {
    Circuit c = sourceResistorLoop();
    CircuitSolver solver;
    auto solution = solver.solve(c);

    auto electron = makeChannelSpecs(c, &solution, 8.0, /*waterWorld=*/false);
    auto water = makeChannelSpecs(c, &solution, 8.0, /*waterWorld=*/true);

    bool electronHasPaddle = false, waterHasPaddle = false;
    for (const auto& spec : electron) electronHasPaddle |= spec.paddle;
    for (const auto& spec : water) waterHasPaddle |= spec.paddle;

    EXPECT_FALSE(electronHasPaddle); // EMF drives charges; no invisible blades
    EXPECT_TRUE(waterHasPaddle);     // the pump impeller is real in the water world
}

TEST(WorldSeparation, BothWorldsKeepDrudeScatterersInTheResistor) {
    Circuit c = sourceResistorLoop();
    CircuitSolver solver;
    auto solution = solver.solve(c);
    for (bool waterWorld : {false, true}) {
        auto specs = makeChannelSpecs(c, &solution, 8.0, waterWorld);
        bool resistorScatters = false;
        for (const auto& spec : specs) resistorScatters |= spec.scatterers;
        EXPECT_TRUE(resistorScatters) << "waterWorld=" << waterWorld;
    }
}

TEST(WorldSeparation, OpenSwitchRemovesTheChannelInBothWorlds) {
    Circuit c = current_lab::demos::buildDemo(current_lab::demos::DemoCircuit::SwitchedRc);
    int switchId = -1;
    for (auto& comp : c.components)
        if (comp.type == ComponentType::Switch) { switchId = comp.id; comp.value = 0.0; }
    ASSERT_GE(switchId, 0);

    CircuitSolver solver;
    auto solution = solver.solve(c);
    for (bool waterWorld : {false, true}) {
        auto specs = makeChannelSpecs(c, &solution, 8.0, waterWorld);
        for (const auto& spec : specs)
            EXPECT_NE(spec.componentId, switchId) << "open switch must have no channel";
    }
}

TEST(WorldSeparation, ElectronsFlowFreelyThroughTheSourceWhereWaterIsStirred) {
    // Same loop, two worlds: the electron source channel must reach the
    // calibrated drift, while the water world has impeller blades there.
    Circuit c = sourceResistorLoop();
    CircuitSolver solver;
    auto solution = solver.solve(c);

    auto electronSpecs = makeChannelSpecs(c, &solution, 8.0, false);
    ParticleSim electronSim;
    electronSim.configure(electronSpecs, 1.2);
    for (int i = 0; i < 240; ++i) electronSim.step(1.0 / 60.0);

    // Mean drift over every electron channel matches its target sign.
    for (const auto& spec : electronSpecs) {
        if (std::abs(spec.targetSpeed) < 1.0) continue;
        Vec2 axis = (spec.b - spec.a).normalized();
        double mean = 0.0;
        int count = 0;
        for (const auto& p : electronSim.particles()) {
            if (p.componentId != spec.componentId) continue;
            mean += p.vel.x * axis.x + p.vel.y * axis.y;
            ++count;
        }
        ASSERT_GT(count, 0);
        mean /= count;
        EXPECT_GT(mean * spec.targetSpeed, 0.0) << "component " << spec.componentId;
    }

    auto waterSpecs = makeChannelSpecs(c, &solution, 8.0, true);
    ParticleSim waterSim;
    waterSim.configure(waterSpecs, 1.2);
    waterSim.step(1.0 / 60.0);
    EXPECT_FALSE(waterSim.paddles().empty()); // impeller exists only here
    EXPECT_TRUE(electronSim.paddles().empty());
}

TEST(WorldSeparation, PaddleAngleAdvancesWithFlow) {
    Circuit c = sourceResistorLoop();
    CircuitSolver solver;
    auto solution = solver.solve(c);
    auto specs = makeChannelSpecs(c, &solution, 8.0, true);

    ParticleSim sim;
    sim.configure(specs, 1.2);
    ASSERT_FALSE(sim.paddles().empty());
    double before = sim.paddles().front().angle;
    for (int i = 0; i < 60; ++i) sim.step(1.0 / 60.0);
    double after = sim.paddles().front().angle;
    EXPECT_GT(std::abs(after - before), 0.05); // the real collider rotates
}

TEST(WorldSeparation, ProjectionsStayDistinctForTheSameModel) {
    // One model, four views: each projection must produce its own picture
    // (no accidental sharing of primitives between worlds).
    Circuit c = sourceResistorLoop();
    CircuitSolver solver;
    auto solution = solver.solve(c);
    ViewParams params;
    params.layers.potential = true;
    params.layers.current = true;
    params.layers.drift = true;

    auto schematic = buildProjection(ProjectionKind::Schematic, c, &solution, params);
    auto physics = buildProjection(ProjectionKind::Physics, c, &solution, params);
    auto mech = buildProjection(ProjectionKind::Mechanical, c, &solution, params);
    auto water = buildProjection(ProjectionKind::Hydraulic, c, &solution, params);

    // Same elements (one model)...
    EXPECT_EQ(schematic.elements.size(), water.elements.size());
    EXPECT_EQ(physics.elements.size(), mech.elements.size());

    // ...different worlds on screen.
    EXPECT_NE(physics.prims.totalCount(), mech.prims.totalCount());
    EXPECT_NE(mech.prims.totalCount(), water.prims.totalCount());
    EXPECT_NE(schematic.prims.totalCount(), water.prims.totalCount());
}
