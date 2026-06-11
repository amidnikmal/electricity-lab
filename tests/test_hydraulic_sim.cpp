#include <gtest/gtest.h>
#include <cmath>
#include "circuit/DemoCircuits.h"
#include "physics/HydraulicSim.h"
#include "solver/CircuitSolver.h"

using namespace current_lab::physics;

namespace {

CircuitSolution solveSimple(const Circuit& c) {
    CircuitSolver solver;
    return solver.solve(c);
}

void runFor(HydraulicSim& sim, double seconds) {
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i)
        sim.step(1.0 / 60.0);
}

} // namespace

TEST(HydraulicSim, ParticlesCountStaysConstant) {
    // Mass conservation: fixed particle pool, count never changes.
    Circuit c = current_lab::demos::buildDemo(current_lab::demos::DemoCircuit::SeriesResistor);
    auto sol = solveSimple(c);

    HydraulicSim sim;
    sim.configure(c, &sol, 8.0);
    auto before = sim.particles();
    ASSERT_GT(before.size(), 0u);
    EXPECT_EQ(before.size(), sim.particles().size());

    runFor(sim, 2.0);
    auto after = sim.particles();
    EXPECT_EQ(after.size(), before.size());
    EXPECT_TRUE(sim.configured());
}

TEST(HydraulicSim, ParticlesAreFiniteAndMove) {
    Circuit c = current_lab::demos::buildDemo(current_lab::demos::DemoCircuit::SeriesResistor);
    auto sol = solveSimple(c);

    HydraulicSim sim;
    sim.configure(c, &sol, 8.0);
    auto before = sim.particles();
    ASSERT_GT(before.size(), 0u);

    runFor(sim, 2.0);
    auto after = sim.particles();
    double moved = 0.0;
    for (size_t i = 0; i < after.size(); ++i) {
        EXPECT_TRUE(std::isfinite(after[i].pos.x));
        EXPECT_TRUE(std::isfinite(after[i].pos.y));
        moved += (after[i].pos - before[i].pos).length();
    }
    EXPECT_GT(moved / after.size(), 1.0); // particles actually flow
}

TEST(HydraulicSim, PaddlesCreatedForSource) {
    Circuit c = current_lab::demos::buildDemo(current_lab::demos::DemoCircuit::SeriesResistor);
    // Ohm's Law demo has ONE voltage source.
    auto sol = solveSimple(c);

    HydraulicSim sim;
    sim.configure(c, &sol, 8.0);
    auto paddles = sim.paddles();
    EXPECT_GE(paddles.size(), 1u);
    // Paddle has a valid componentId.
    for (const auto& p : paddles)
        EXPECT_GE(p.componentId, 0);
}

TEST(HydraulicSim, ResistorCircuitIsComplete) {
    // A resistor-only circuit (no source) should still build a loop.
    Circuit c;
    int n0 = c.addNode(Vec2(0, 0));
    int n1 = c.addNode(Vec2(200, 0));
    c.groundNodeId = n0;
    c.components.push_back({c.nextComponentId++, ComponentType::Wire, n0, n1, 0.0});
    c.components.push_back({c.nextComponentId++, ComponentType::Wire, n1, n0, 0.0});

    CircuitSolution emptySol;
    HydraulicSim sim;
    sim.configure(c, &emptySol, 8.0);
    EXPECT_TRUE(sim.configured());
    EXPECT_GT(sim.particles().size(), 0u);
}

TEST(HydraulicSim, EmptyCircuitIsHandled) {
    Circuit c;
    HydraulicSim sim;
    sim.configure(c, nullptr, 8.0);
    EXPECT_FALSE(sim.configured());
    EXPECT_TRUE(sim.particles().empty());
    sim.step(1.0 / 60.0); // should not crash
}
