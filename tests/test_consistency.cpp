#include <gtest/gtest.h>
#include <cmath>
#include <unordered_map>
#include "solver/CircuitSolver.h"

static constexpr double kEps = 1e-9;

struct TestCircuit {
    Circuit circuit;
    CircuitSolution solution;
};

static TestCircuit makeSeriesR() {
    TestCircuit tc;
    auto& c = tc.circuit;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Wire, 2, 0);
    tc.solution = CircuitSolver{}.solve(c);
    return tc;
}

static TestCircuit makeVoltageDivider() {
    TestCircuit tc;
    auto& c = tc.circuit;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 12.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Resistor, 2, 0, 2000.0);
    tc.solution = CircuitSolver{}.solve(c);
    return tc;
}

static TestCircuit makeParallelR() {
    TestCircuit tc;
    auto& c = tc.circuit;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 10.0);
    c.addComponent(ComponentType::Resistor, 1, 0, 1000.0);
    c.addComponent(ComponentType::Resistor, 1, 0, 500.0);
    tc.solution = CircuitSolver{}.solve(c);
    return tc;
}

static TestCircuit makeTwoSources() {
    TestCircuit tc;
    auto& c = tc.circuit;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 3.0);
    c.addComponent(ComponentType::VoltageSource, 2, 1, 2.0);
    c.addComponent(ComponentType::Resistor, 2, 0, 100.0);
    tc.solution = CircuitSolver{}.solve(c);
    return tc;
}

static TestCircuit makeComplexMesh() {
    TestCircuit tc;
    auto& c = tc.circuit;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.addNode({150, 100});
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 9.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 300.0);
    c.addComponent(ComponentType::Resistor, 2, 3, 600.0);
    c.addComponent(ComponentType::Resistor, 3, 0, 200.0);
    c.addComponent(ComponentType::Resistor, 2, 0, 100.0);
    tc.solution = CircuitSolver{}.solve(c);
    return tc;
}

static TestCircuit makeNonZeroGround() {
    TestCircuit tc;
    auto& c = tc.circuit;
    c.addNode({0, 0});
    c.addNode({100, 0});
    c.addNode({200, 0});
    c.groundNodeId = 2;
    c.addComponent(ComponentType::VoltageSource, 0, 1, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 100.0);
    c.addComponent(ComponentType::Resistor, 0, 2, 200.0);
    tc.solution = CircuitSolver{}.solve(c);
    return tc;
}

static double nodePotential(const CircuitSolution& sol, int nodeId) {
    for (const auto& np : sol.nodePotentials)
        if (np.nodeId == nodeId) return np.potential;
    return std::nan("");
}

static void checkKCL(const TestCircuit& tc) {
    const auto& c = tc.circuit;
    const auto& sol = tc.solution;

    std::unordered_map<int, double> netCurrent;
    for (const auto& comp : c.components) {
        if (comp.type == ComponentType::Ground) continue;
        for (const auto& br : sol.branches) {
            if (br.componentId != comp.id) continue;
            netCurrent[comp.nodeA] -= br.current;
            netCurrent[comp.nodeB] += br.current;
        }
    }
    int groundId = c.groundNodeId;
    if (groundId < 0 || groundId >= static_cast<int>(c.nodes.size())) groundId = 0;

    for (const auto& [nodeId, netI] : netCurrent) {
        if (nodeId == groundId) continue;
        EXPECT_NEAR(netI, 0.0, 1e-6)
            << "KCL violated at node " << nodeId << ": net current = " << netI;
    }
}

static void checkPowerBalance(const TestCircuit& tc) {
    double totalPower = 0.0;
    for (const auto& br : tc.solution.branches)
        totalPower += br.power;
    EXPECT_NEAR(totalPower, 0.0, 1e-6)
        << "Power balance violated: total = " << totalPower;
}

static void checkTellegen(const TestCircuit& tc) {
    const auto& sol = tc.solution;
    double sum = 0.0;
    for (const auto& br : sol.branches)
        sum += br.current * br.voltageDrop;
    EXPECT_NEAR(sum, 0.0, 1e-6)
        << "Tellegen violated: sum(V*I) = " << sum;
}

TEST(Consistency, KCL_SeriesR) { checkKCL(makeSeriesR()); }
TEST(Consistency, KCL_VoltageDivider) { checkKCL(makeVoltageDivider()); }
TEST(Consistency, KCL_ParallelR) { checkKCL(makeParallelR()); }
TEST(Consistency, KCL_TwoSources) { checkKCL(makeTwoSources()); }
TEST(Consistency, KCL_ComplexMesh) { checkKCL(makeComplexMesh()); }
TEST(Consistency, KCL_NonZeroGround) { checkKCL(makeNonZeroGround()); }

TEST(Consistency, PowerBalance_SeriesR) { checkPowerBalance(makeSeriesR()); }
TEST(Consistency, PowerBalance_VoltageDivider) { checkPowerBalance(makeVoltageDivider()); }
TEST(Consistency, PowerBalance_ParallelR) { checkPowerBalance(makeParallelR()); }
TEST(Consistency, PowerBalance_TwoSources) { checkPowerBalance(makeTwoSources()); }
TEST(Consistency, PowerBalance_ComplexMesh) { checkPowerBalance(makeComplexMesh()); }
TEST(Consistency, PowerBalance_NonZeroGround) { checkPowerBalance(makeNonZeroGround()); }

TEST(Consistency, Tellegen_SeriesR) { checkTellegen(makeSeriesR()); }
TEST(Consistency, Tellegen_VoltageDivider) { checkTellegen(makeVoltageDivider()); }
TEST(Consistency, Tellegen_ParallelR) { checkTellegen(makeParallelR()); }
TEST(Consistency, Tellegen_TwoSources) { checkTellegen(makeTwoSources()); }
TEST(Consistency, Tellegen_ComplexMesh) { checkTellegen(makeComplexMesh()); }
TEST(Consistency, Tellegen_NonZeroGround) { checkTellegen(makeNonZeroGround()); }

TEST(Consistency, AllPotentialsFinite) {
    auto tc = makeComplexMesh();
    for (const auto& np : tc.solution.nodePotentials)
        EXPECT_TRUE(std::isfinite(np.potential));
}

TEST(Consistency, AllBranchValuesFinite) {
    auto tc = makeComplexMesh();
    for (const auto& br : tc.solution.branches) {
        EXPECT_TRUE(std::isfinite(br.current));
        EXPECT_TRUE(std::isfinite(br.voltageDrop));
        EXPECT_TRUE(std::isfinite(br.power));
    }
}

TEST(Consistency, GroundPotentialZero) {
    auto tc = makeNonZeroGround();
    EXPECT_NEAR(nodePotential(tc.solution, 2), 0.0, kEps);
}

TEST(Consistency, OhmLawHolds) {
    auto tc = makeComplexMesh();
    for (const auto& comp : tc.circuit.components) {
        if (comp.type != ComponentType::Resistor) continue;
        double Va = nodePotential(tc.solution, comp.nodeA);
        double Vb = nodePotential(tc.solution, comp.nodeB);
        for (const auto& br : tc.solution.branches) {
            if (br.componentId != comp.id) continue;
            double expectedI = (Va - Vb) / comp.value;
            EXPECT_NEAR(br.current, expectedI, 1e-7)
                << "Ohm's law violated for resistor " << comp.id;
        }
    }
}

static double branchVoltageFor(const CircuitSolution& sol, int compId) {
    for (const auto& br : sol.branches)
        if (br.componentId == compId) return br.voltageDrop;
    return std::nan("");
}

TEST(Consistency, KVLLoop_SeriesR) {
    auto tc = makeSeriesR();
    double Vvs = branchVoltageFor(tc.solution, 0);
    double Vr  = branchVoltageFor(tc.solution, 1);
    double Vw  = branchVoltageFor(tc.solution, 2);

    double loop = 0.0;
    loop -= Vvs;
    loop += Vr;
    loop += Vw;
    EXPECT_NEAR(loop, 0.0, 1e-6);
}

TEST(Consistency, KVLLoop_VoltageDivider) {
    auto tc = makeVoltageDivider();
    double Vvs = branchVoltageFor(tc.solution, 0);
    double Vr1 = branchVoltageFor(tc.solution, 1);
    double Vr2 = branchVoltageFor(tc.solution, 2);

    double loop = 0.0;
    loop -= Vvs;
    loop += Vr1;
    loop += Vr2;
    EXPECT_NEAR(loop, 0.0, 1e-6);
}

TEST(Consistency, KVLLoop_TwoSources) {
    auto tc = makeTwoSources();
    double Vvs1 = branchVoltageFor(tc.solution, 0);
    double Vvs2 = branchVoltageFor(tc.solution, 1);
    double Vr   = branchVoltageFor(tc.solution, 2);

    double loop = 0.0;
    loop -= Vvs1;
    loop -= Vvs2;
    loop += Vr;
    EXPECT_NEAR(loop, 0.0, 1e-6);
}
