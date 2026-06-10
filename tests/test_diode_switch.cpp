#include <gtest/gtest.h>
#include <cmath>
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "projection/ProjectionBuilder.h"

namespace {

const BranchResult* branchFor(const CircuitSolution& s, int id) {
    for (const auto& br : s.branches)
        if (br.componentId == id) return &br;
    return nullptr;
}

struct DiodeCircuit {
    Circuit c;
    int srcId = -1, resId = -1, diodeId = -1;
};

DiodeCircuit makeDiodeSeries(double V) {
    DiodeCircuit d;
    int gnd = d.c.addNode(Vec2(0, 100));
    int n1 = d.c.addNode(Vec2(0, 0));
    int n2 = d.c.addNode(Vec2(150, 0));
    d.c.groundNodeId = gnd;
    d.c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    d.srcId = d.c.addComponent(ComponentType::VoltageSource, n1, gnd, V);
    d.diodeId = d.c.addComponent(ComponentType::Diode, n1, n2, 0.0);
    d.resId = d.c.addComponent(ComponentType::Resistor, n2, gnd, 1000.0);
    return d;
}

} // namespace

TEST(Diode, ConductsWhenForwardBiased) {
    DiodeCircuit d = makeDiodeSeries(5.0);
    CircuitSolver solver;
    auto solution = solver.solve(d.c);

    const BranchResult* res = branchFor(solution, d.resId);
    const BranchResult* diode = branchFor(solution, d.diodeId);
    ASSERT_NE(res, nullptr);
    ASSERT_NE(diode, nullptr);
    EXPECT_NEAR(res->current, 5.0 / 1000.0, 1e-6);     // full Ohm's-law current
    EXPECT_NEAR(diode->voltageDrop, 0.0, 1e-6);         // ideal: no forward drop
    EXPECT_GT(diode->current, 0.0);
}

TEST(Diode, BlocksWhenReverseBiased) {
    DiodeCircuit d = makeDiodeSeries(-5.0);
    CircuitSolver solver;
    auto solution = solver.solve(d.c);

    const BranchResult* res = branchFor(solution, d.resId);
    const BranchResult* diode = branchFor(solution, d.diodeId);
    ASSERT_NE(res, nullptr);
    ASSERT_NE(diode, nullptr);
    EXPECT_NEAR(res->current, 0.0, 1e-9);               // no current flows
    EXPECT_NEAR(diode->voltageDrop, -5.0, 1e-6);        // blocks the full voltage
}

TEST(Diode, PeakDetectorHoldsCapacitorCharge) {
    // V -> diode -> C: charge the cap, then drop the source to zero. The
    // diode must block the discharge path, so Vc holds.
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(150, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    int srcId = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Diode, n1, n2, 0.0);
    int capId = c.addComponent(ComponentType::Capacitor, n2, gnd, 1e-3);

    CircuitSolver solver;
    TransientState state;
    for (int i = 0; i < 200; ++i)
        solver.stepTransient(c, state, 1e-3);
    double charged = state.capVoltage[capId];
    EXPECT_GT(charged, 4.5); // charges fast through the ideal diode

    Component* src = c.findComponent(srcId);
    ASSERT_NE(src, nullptr);
    src->value = 0.0;
    for (int i = 0; i < 500; ++i)
        solver.stepTransient(c, state, 1e-3);

    EXPECT_NEAR(state.capVoltage[capId], charged, 0.01); // held by the diode
}

TEST(Switch, OpenStopsCurrentClosedPassesIt) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(150, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    int swId = c.addComponent(ComponentType::Switch, n1, n2, 1.0); // closed
    int resId = c.addComponent(ComponentType::Resistor, n2, gnd, 1000.0);

    CircuitSolver solver;
    auto closed = solver.solve(c);
    EXPECT_NEAR(branchFor(closed, resId)->current, 0.005, 1e-6);

    c.findComponent(swId)->value = 0.0; // open
    auto open = solver.solve(c);
    EXPECT_NEAR(branchFor(open, resId)->current, 0.0, 1e-9);
}

TEST(Switch, OpeningMidTransientFreezesCapacitorCharge) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(150, 0));
    int n3 = c.addNode(Vec2(300, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    int swId = c.addComponent(ComponentType::Switch, n1, n2, 1.0);
    c.addComponent(ComponentType::Resistor, n2, n3, 1000.0);
    int capId = c.addComponent(ComponentType::Capacitor, n3, gnd, 1e-3);

    CircuitSolver solver;
    TransientState state;
    for (int i = 0; i < 500; ++i)
        solver.stepTransient(c, state, 1e-3); // charge for 0.5 tau
    double midway = state.capVoltage[capId];
    EXPECT_GT(midway, 1.0);
    EXPECT_LT(midway, 4.0);

    c.findComponent(swId)->value = 0.0; // open the switch
    for (int i = 0; i < 1000; ++i)
        solver.stepTransient(c, state, 1e-3);

    EXPECT_NEAR(state.capVoltage[capId], midway, 0.01); // charge frozen
}

TEST(DiodeSwitchProjection, SymbolsAppearInAllProjections) {
    DiodeCircuit d = makeDiodeSeries(5.0);
    int swId = d.c.addComponent(ComponentType::Switch, d.c.nodes[1].id, d.c.nodes[0].id, 0.0);
    (void)swId;

    CircuitSolver solver;
    auto solution = solver.solve(d.c);
    current_lab::projection::ViewParams params;

    for (auto kind : {current_lab::projection::ProjectionKind::Schematic,
                      current_lab::projection::ProjectionKind::Physics,
                      current_lab::projection::ProjectionKind::Spintronics}) {
        auto result = current_lab::projection::buildProjection(kind, d.c, &solution, params);
        EXPECT_TRUE(current_lab::projection::projectionHasComponent(result, d.diodeId));
        EXPECT_FALSE(result.prims.arrows.empty()); // diode triangle / ratchet pawl
    }

    auto schematic = current_lab::projection::buildProjection(
        current_lab::projection::ProjectionKind::Schematic, d.c, &solution, params);
    bool hasOpenLabel = false;
    for (const auto& label : schematic.prims.labels)
        hasOpenLabel = hasOpenLabel || label.text == "open";
    EXPECT_TRUE(hasOpenLabel);
}
