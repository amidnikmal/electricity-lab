#include <gtest/gtest.h>
#include <cmath>
#include "math/LinearSystem.h"
#include "physics/WirePhysics.h"
#include "solver/CircuitSolver.h"

static constexpr double kEps = 1e-9;

static Circuit makeEmptyCircuit() {
    Circuit c;
    c.addNode({0, 0});
    c.groundNodeId = 0;
    return c;
}

static Circuit makeSeriesRCircuit() {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Wire, 2, 0);
    return c;
}

static Circuit makeVoltageDivider() {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 12.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Resistor, 2, 0, 2000.0);
    return c;
}

static Circuit makeParallelResistors() {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 10.0);
    c.addComponent(ComponentType::Resistor, 1, 0, 1000.0);
    c.addComponent(ComponentType::Resistor, 1, 0, 500.0);
    return c;
}

static Circuit makeTwoVoltageSources() {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 3.0);
    c.addComponent(ComponentType::VoltageSource, 2, 1, 2.0);
    c.addComponent(ComponentType::Resistor, 2, 0, 100.0);
    return c;
}

static Circuit makeFloatingResistor() {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::Resistor, 1, 2, 100.0);
    return c;
}

static Circuit makeFloatingSourceWithOpenEnd() {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 100.0);
    return c;
}

static Circuit makeNonZeroGround() {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 2;
    c.addComponent(ComponentType::VoltageSource, 0, 1, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 100.0);
    c.addComponent(ComponentType::Resistor, 0, 2, 200.0);
    return c;
}

static double findPotential(const CircuitSolution& sol, int nodeId) {
    for (const auto& np : sol.nodePotentials)
        if (np.nodeId == nodeId) return np.potential;
    return std::nan("");
}

static const BranchResult* findBranch(const CircuitSolution& sol, int compId) {
    for (const auto& br : sol.branches)
        if (br.componentId == compId) return &br;
    return nullptr;
}

TEST(Solver, EmptyCircuit) {
    Circuit c = makeEmptyCircuit();
    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_EQ(sol.nodePotentials.size(), 1u);
    EXPECT_NEAR(findPotential(sol, 0), 0.0, kEps);
    EXPECT_TRUE(sol.branches.empty());
}

TEST(Solver, SeriesCircuitVoltageSourceCurrent) {
    Circuit c = makeSeriesRCircuit();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    EXPECT_NEAR(findPotential(sol, 0), 0.0, kEps);
    EXPECT_NEAR(findPotential(sol, 1), 5.0, kEps);
    EXPECT_NEAR(std::abs(findPotential(sol, 2)), 0.0, 1e-5);

    auto* vs = findBranch(sol, 0);
    ASSERT_NE(vs, nullptr);
    EXPECT_NEAR(vs->current, -0.005, 1e-7);
    EXPECT_NEAR(vs->voltageDrop, 5.0, kEps);

    auto* r = findBranch(sol, 1);
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->current, 0.005, 1e-7);
    EXPECT_NEAR(r->voltageDrop, 5.0, kEps);

    auto* w = findBranch(sol, 2);
    ASSERT_NE(w, nullptr);
    EXPECT_NEAR(std::abs(w->current - 0.005), 0.0, 1e-7);
}

TEST(Solver, VoltageDivider) {
    Circuit c = makeVoltageDivider();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    EXPECT_NEAR(findPotential(sol, 0), 0.0, kEps);
    EXPECT_NEAR(findPotential(sol, 1), 12.0, kEps);
    EXPECT_NEAR(findPotential(sol, 2), 8.0, kEps);

    auto* vs = findBranch(sol, 0);
    ASSERT_NE(vs, nullptr);
    EXPECT_NEAR(vs->current, -0.004, 1e-7);
    EXPECT_NEAR(vs->voltageDrop, 12.0, kEps);

    auto* r1 = findBranch(sol, 1);
    ASSERT_NE(r1, nullptr);
    EXPECT_NEAR(r1->current, 0.004, 1e-7);
    EXPECT_NEAR(r1->voltageDrop, 4.0, kEps);

    auto* r2 = findBranch(sol, 2);
    ASSERT_NE(r2, nullptr);
    EXPECT_NEAR(r2->current, 0.004, 1e-7);
    EXPECT_NEAR(r2->voltageDrop, 8.0, kEps);
}

TEST(Solver, ParallelResistors) {
    Circuit c = makeParallelResistors();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    EXPECT_NEAR(findPotential(sol, 0), 0.0, kEps);
    EXPECT_NEAR(findPotential(sol, 1), 10.0, kEps);

    auto* r1 = findBranch(sol, 1);
    ASSERT_NE(r1, nullptr);
    EXPECT_NEAR(r1->current, 0.010, 1e-7);
    EXPECT_NEAR(r1->voltageDrop, 10.0, kEps);

    auto* r2 = findBranch(sol, 2);
    ASSERT_NE(r2, nullptr);
    EXPECT_NEAR(r2->current, 0.020, 1e-7);
    EXPECT_NEAR(r2->voltageDrop, 10.0, kEps);

    auto* vs = findBranch(sol, 0);
    ASSERT_NE(vs, nullptr);
    EXPECT_NEAR(vs->current, -0.030, 1e-7);
}

TEST(Solver, TwoVoltageSourcesInSeries) {
    Circuit c = makeTwoVoltageSources();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    EXPECT_NEAR(findPotential(sol, 0), 0.0, kEps);
    EXPECT_NEAR(findPotential(sol, 1), 3.0, kEps);
    EXPECT_NEAR(findPotential(sol, 2), 5.0, kEps);

    auto* vs1 = findBranch(sol, 0);
    ASSERT_NE(vs1, nullptr);
    EXPECT_NEAR(vs1->voltageDrop, 3.0, kEps);

    auto* vs2 = findBranch(sol, 1);
    ASSERT_NE(vs2, nullptr);
    EXPECT_NEAR(vs2->voltageDrop, 2.0, kEps);

    auto* r = findBranch(sol, 2);
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->current, 0.050, 1e-7);
    EXPECT_NEAR(r->voltageDrop, 5.0, kEps);
}

TEST(Solver, ZeroNodeCircuit) {
    Circuit c;
    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_TRUE(sol.nodePotentials.empty());
    EXPECT_TRUE(sol.branches.empty());
}

TEST(Solver, FloatingResistorNoSource) {
    Circuit c = makeFloatingResistor();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    EXPECT_NEAR(findPotential(sol, 0), 0.0, kEps);
    EXPECT_NEAR(findPotential(sol, 1), 0.0, kEps);
    EXPECT_NEAR(findPotential(sol, 2), 0.0, kEps);

    auto* r = findBranch(sol, 0);
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->current, 0.0, kEps);
    EXPECT_NEAR(r->voltageDrop, 0.0, kEps);
}

TEST(Solver, FloatingSourceWithOpenNode) {
    Circuit c = makeFloatingSourceWithOpenEnd();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    EXPECT_NEAR(findPotential(sol, 1), 5.0, kEps);
    EXPECT_NEAR(findPotential(sol, 2), 5.0, kEps);

    auto* vs = findBranch(sol, 0);
    ASSERT_NE(vs, nullptr);
    EXPECT_NEAR(vs->voltageDrop, 5.0, kEps);
    EXPECT_NEAR(vs->current, 0.0, 1e-8);

    auto* r = findBranch(sol, 1);
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->current, 0.0, kEps);
    EXPECT_NEAR(r->voltageDrop, 0.0, kEps);
}

TEST(Solver, NonZeroGroundNode) {
    Circuit c = makeNonZeroGround();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    EXPECT_NEAR(findPotential(sol, 2), 0.0, kEps);

    double V0 = findPotential(sol, 0);
    double V1 = findPotential(sol, 1);
    EXPECT_NEAR(V0 - V1, 5.0, kEps);

    auto* vs = findBranch(sol, 0);
    ASSERT_NE(vs, nullptr);
    EXPECT_NEAR(vs->voltageDrop, 5.0, kEps);

    auto* r1 = findBranch(sol, 1);
    ASSERT_NE(r1, nullptr);
    EXPECT_NEAR(r1->current, V1 / 100.0, 1e-7);

    auto* r2 = findBranch(sol, 2);
    ASSERT_NE(r2, nullptr);
    EXPECT_NEAR(r2->current, V0 / 200.0, 1e-7);
}

TEST(Solver, WireInSeriesWithResistor) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 10.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Wire, 2, 0);

    CircuitSolver solver;
    auto sol = solver.solve(c);

    EXPECT_NEAR(findPotential(sol, 1), 10.0, kEps);
    EXPECT_NEAR(std::abs(findPotential(sol, 2)), 0.0, 1e-5);

    auto* r = findBranch(sol, 1);
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->current, 0.010, 1e-7);

    auto* w = findBranch(sol, 2);
    ASSERT_NE(w, nullptr);
    EXPECT_NEAR(std::abs(w->current - 0.010), 0.0, 1e-7);
    EXPECT_NEAR(std::abs(w->voltageDrop), 0.0, 1e-5);
}

TEST(Solver, AllNodePotentialsReturned) {
    Circuit c = makeSeriesRCircuit();
    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_EQ(sol.nodePotentials.size(), 3u);

    bool found0 = false, found1 = false, found2 = false;
    for (const auto& np : sol.nodePotentials) {
        if (np.nodeId == 0) found0 = true;
        if (np.nodeId == 1) found1 = true;
        if (np.nodeId == 2) found2 = true;
    }
    EXPECT_TRUE(found0 && found1 && found2);
}

TEST(Solver, GroundNodesExcludedFromBranches) {
    Circuit c;
    c.addNode({0, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::Ground, 0, 0);

    CircuitSolver solver;
    auto sol = solver.solve(c);
    for (const auto& br : sol.branches)
        EXPECT_NE(br.componentId, 0);
}

TEST(Solver, PowerSignSourceSupplies) {
    Circuit c = makeSeriesRCircuit();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    auto* vs = findBranch(sol, 0);
    ASSERT_NE(vs, nullptr);
    EXPECT_LT(vs->power, 0.0);

    auto* r = findBranch(sol, 1);
    ASSERT_NE(r, nullptr);
    EXPECT_GT(r->power, 0.0);

    auto* w = findBranch(sol, 2);
    ASSERT_NE(w, nullptr);
    EXPECT_GT(w->power, 0.0);
}

TEST(Solver, SeriesCircuitResistorPowerIs25mW) {
    Circuit c = makeSeriesRCircuit();
    CircuitSolver solver;
    auto sol = solver.solve(c);

    auto* r = findBranch(sol, 1);
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->current, 0.005, 1e-7);
    EXPECT_NEAR(r->power, 0.025, 1e-7);
}

TEST(Solver, NearZeroOhmResistorBehavesLikeWire) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 1.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Resistor, 2, 0, 1e-9);

    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_NEAR(findPotential(sol, 1), 1.0, kEps);
    EXPECT_NEAR(std::abs(findPotential(sol, 2)), 0.0, 1e-5);

    auto* r = findBranch(sol, 1);
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->current, 0.001, 1e-7);

    for (const auto& br : sol.branches) {
        EXPECT_TRUE(std::isfinite(br.current));
        EXPECT_TRUE(std::isfinite(br.voltageDrop));
    }
}

TEST(Solver, UnsetGroundFallsBackToNodeZero) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = -1;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Wire, 2, 0);

    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_NEAR(findPotential(sol, 0), 0.0, kEps);
    EXPECT_NEAR(findPotential(sol, 1), 5.0, kEps);
    EXPECT_TRUE(std::abs(findPotential(sol, 2)) < 1e-5);
}

TEST(Solver, ParallelVoltageSourcesProducesFiniteResults) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);

    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_NEAR(findPotential(sol, 1), 5.0, kEps);
    for (const auto& br : sol.branches)
        EXPECT_TRUE(std::isfinite(br.current));
}

TEST(Solver, NonContiguousNodeIdsAreSolvedCorrectly) {
    Circuit c;
    int gnd = c.addNode({0, 0});
    c.addNode({25, 0}); // removed to create a gap in ids
    int src = c.addNode({100, 0});
    int load = c.addNode({200, 0});
    c.removeNode(1);
    c.groundNodeId = gnd;

    c.addComponent(ComponentType::VoltageSource, src, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, src, load, 1000.0);
    c.addComponent(ComponentType::Wire, load, gnd);

    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_NEAR(findPotential(sol, gnd), 0.0, kEps);
    EXPECT_NEAR(findPotential(sol, src), 5.0, kEps);
    EXPECT_NEAR(std::abs(findPotential(sol, load)), 0.0, 1e-5);

    auto* r = findBranch(sol, 1);
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->current, 0.005, 1e-7);
}

TEST(Solver, WireResistanceScalesWithLength) {
    double r100 = current_lab::physics::wireResistance(100.0);
    double r200 = current_lab::physics::wireResistance(200.0);
    EXPECT_NEAR(r200, r100 * 2.0, kEps);
}

TEST(Solver, SegmentCountDoesNotChangeTotalWireResistance) {
    double r4 = current_lab::physics::segmentResistance(120.0, 4) * 4.0;
    double r12 = current_lab::physics::segmentResistance(120.0, 12) * 12.0;
    EXPECT_NEAR(r4, r12, kEps);
}

TEST(Solver, DistributedWireHasNonZeroVoltageDrop) {
    Circuit c;
    c.addNode({0, 0});   // node 0 — ground
    c.addNode({100, 0});  // node 1
    c.addNode({200, 0});  // node 2
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Wire, 0, 2);  // ideal: node2 = node0 = 0V

    CircuitSolver solver;
    auto solIdeal = solver.solve(c);
    EXPECT_NEAR(findPotential(solIdeal, 0), 0.0, 1e-9);
    EXPECT_NEAR(findPotential(solIdeal, 2), 0.0, 1e-9);

    Circuit d = c.toDistributed(8);
    auto solDist = solver.solve(d);

    // Node 0 is ground (0V); node 2 is the far end of the distributed chain
    // The distributed wire is a chain of 8 small resistors — voltage at node 2
    // should now be slightly above 0V due to the voltage drop across the chain.
    EXPECT_NEAR(findPotential(solDist, 0), 0.0, 1e-9);
    double v2 = findPotential(solDist, 2);
    EXPECT_GT(v2, 1e-6);  // non-zero wire drop
    EXPECT_LT(v2, 0.5);   // much less than full 5V

    // Intermediate nodes should have monotonically increasing potential from 0 to v2
    // Each wire-segment resistor has R = wireLength * rhoPerUnit / N
    // Which gives a non-trivial voltage gradient along the wire.
}

TEST(Solver, DistributedWireGradientMonotonic) {
    // Same as above but verify the wire voltage gradient is monotonic
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Wire, 0, 2);

    Circuit d = c.toDistributed(8);
    CircuitSolver solver;
    auto sol = solver.solve(d);

    // Nodes 0..9: [ground, v1=5V, intermediate0..6, node2=far end]
    // toDistributed creates 8 small resistors between node0 and node2.
    // Original: node0(0) --wire--> node2(2)
    // Distributed: 0--R0--Nnew0--R1--Nnew1--...--R7--2
    // With nodes: 0=ground, 1=Vsrc, 2=far_end, 3..9=intermediate
    // Wire goes 0→2, so intermediates are between 0 and 2 in potential
    double prev = 0.0;
    for (size_t i = 3; i < d.nodes.size(); ++i) {
        double vi = findPotential(sol, (int)i);
        EXPECT_GE(vi, prev - 1e-12);  // monotonic non-decreasing
        prev = vi;
    }
}

TEST(Solver, DistributedWireSegmentsCarrySameSeriesCurrent) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    int wireId = c.addComponent(ComponentType::Wire, 0, 2);

    Circuit d = c.toDistributed(8);
    CircuitSolver solver;
    auto sol = solver.solve(d);

    double firstCurrent = std::nan("");
    for (size_t i = 0; i < sol.branches.size(); ++i) {
        if (d.distributedSource[i] != wireId) continue;
        if (!std::isfinite(firstCurrent))
            firstCurrent = sol.branches[i].current;
        EXPECT_NEAR(sol.branches[i].current, firstCurrent, 1e-7);
    }
}

// --- LinearSolveResult status tests ---

TEST(Solver, NormalCircuitHasOkStatus) {
    Circuit c = makeSeriesRCircuit();
    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_EQ(sol.solveStatus, "ok");
}

TEST(Solver, FloatingNodeCircuitHasSingularStatus) {
    Circuit c = makeFloatingResistor();
    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_EQ(sol.solveStatus, "singular");
}

TEST(Solver, IllConditionedCircuitStatus) {
    Circuit c;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 0, 1e-9);
    c.addComponent(ComponentType::Resistor, 1, 0, 1e9);

    CircuitSolver solver;
    auto sol = solver.solve(c);
    EXPECT_TRUE(sol.solveStatus == "ill-conditioned" || sol.solveStatus == "ok");
}

TEST(Solver, SolveStatusPresentInAllSolveMethods) {
    Circuit c = makeSeriesRCircuit();
    CircuitSolver solver;

    auto solDc = solver.solve(c);
    EXPECT_FALSE(solDc.solveStatus.empty());

    TransientState state;
    auto solStep = solver.stepTransient(c, state, 1e-6);
    EXPECT_FALSE(solStep.solveStatus.empty());

    auto solSnap = solver.solveTransientSnapshot(c, state);
    EXPECT_FALSE(solSnap.solveStatus.empty());
}

TEST(LinearSystemDiagnostic, NormalSystemOk) {
    LinearSystem s;
    s.resize(3);
    s.A = {{3, 2, -1}, {2, -2, 4}, {-1, 0.5, -1}};
    s.b = {1, -2, 0};
    auto r = s.solve();
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.status, "ok");
    EXPECT_EQ(r.rank, 3);
    EXPECT_GT(r.rcond, 0.0);
    EXPECT_LT(r.residual, 1e-10);
}

TEST(LinearSystemDiagnostic, SingularSystem) {
    LinearSystem s;
    s.resize(2);
    s.A = {{1, 2}, {2, 4}};
    s.b = {3, 6};
    auto r = s.solve();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.status, "singular");
    EXPECT_TRUE(r.singular);
    EXPECT_EQ(r.rank, 1);
}

TEST(LinearSystemDiagnostic, ZeroRowIsSingular) {
    LinearSystem s;
    s.resize(3);
    s.A = {{1, 1, 1}, {0, 0, 0}, {0, 1, -1}};
    s.b = {6, 0, 1};
    auto r = s.solve();
    EXPECT_EQ(r.status, "singular");
    EXPECT_TRUE(r.singular);
}

TEST(LinearSystemDiagnostic, IllConditionedMatrix) {
    LinearSystem s;
    s.resize(2);
    s.A = {{1e9, 1}, {1, 0}};
    s.b = {0, 5};
    auto r = s.solve();
    EXPECT_EQ(r.status, "ill-conditioned");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.rank, 2);
    EXPECT_LT(r.rcond, 1e-14);
}

TEST(LinearSystemDiagnostic, InconsistentSystem) {
    LinearSystem s;
    s.resize(2);
    s.A = {{1, 1}, {1, 1}};
    s.b = {3, 5};
    auto r = s.solve();
    EXPECT_FALSE(r.ok);
    EXPECT_GT(r.residual, 1e-6);
}
