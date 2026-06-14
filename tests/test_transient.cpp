#include <gtest/gtest.h>
#include <cmath>
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"

// Transient solver validation against analytic RC/RL solutions.
// Method: MNA with companion models, backward Euler (default) or trapezoidal.

namespace {

struct RcCircuit {
    Circuit c;
    int srcId = -1, resId = -1, capId = -1;
};

RcCircuit makeRcCharge(double V, double R, double C) {
    RcCircuit rc;
    int gnd = rc.c.addNode(Vec2(0, 100));
    int n1 = rc.c.addNode(Vec2(0, 0));
    int n2 = rc.c.addNode(Vec2(100, 0));
    rc.c.groundNodeId = gnd;
    rc.c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    rc.srcId = rc.c.addComponent(ComponentType::VoltageSource, n1, gnd, V);
    rc.resId = rc.c.addComponent(ComponentType::Resistor, n1, n2, R);
    rc.capId = rc.c.addComponent(ComponentType::Capacitor, n2, gnd, C);
    return rc;
}

struct RlCircuit {
    Circuit c;
    int srcId = -1, resId = -1, indId = -1;
};

RlCircuit makeRlRise(double V, double R, double L) {
    RlCircuit rl;
    int gnd = rl.c.addNode(Vec2(0, 100));
    int n1 = rl.c.addNode(Vec2(0, 0));
    int n2 = rl.c.addNode(Vec2(100, 0));
    rl.c.groundNodeId = gnd;
    rl.c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    rl.srcId = rl.c.addComponent(ComponentType::VoltageSource, n1, gnd, V);
    rl.resId = rl.c.addComponent(ComponentType::Resistor, n1, n2, R);
    rl.indId = rl.c.addComponent(ComponentType::Inductor, n2, gnd, L);
    return rl;
}

double stateCapV(const TransientState& s, int id) {
    auto it = s.capVoltage.find(id);
    return it == s.capVoltage.end() ? 0.0 : it->second;
}

double stateCapQ(const TransientState& s, int id) {
    auto it = s.capCharge.find(id);
    return it == s.capCharge.end() ? 0.0 : it->second;
}

double stateIndI(const TransientState& s, int id) {
    auto it = s.indCurrent.find(id);
    return it == s.indCurrent.end() ? 0.0 : it->second;
}

const BranchResult* branchFor(const CircuitSolution& s, int id) {
    for (const auto& br : s.branches)
        if (br.componentId == id) return &br;
    return nullptr;
}

double simulateRcChargeTo(double tEnd, double dt, const RcCircuit& rc, TransientState& state,
                          IntegrationMethod method = IntegrationMethod::BackwardEuler) {
    CircuitSolver solver;
    Circuit circuit = rc.c;
    int steps = static_cast<int>(std::round(tEnd / dt));
    for (int i = 0; i < steps; ++i)
        solver.stepTransient(circuit, state, dt, method);
    return stateCapV(state, rc.capId);
}

} // namespace

TEST(TransientRC, ChargeReaches632PercentAtTau) {
    // V=5, R=1k, C=1mF -> tau = RC = 1 s.
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
    TransientState state;
    double vc = simulateRcChargeTo(1.0, 1e-3, rc, state);
    EXPECT_NEAR(vc, 5.0 * (1.0 - std::exp(-1.0)), 0.02);
}

TEST(TransientRC, ChargeApproachesSourceVoltage) {
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
    TransientState state;
    double vc = simulateRcChargeTo(10.0, 1e-3, rc, state); // 10 tau
    EXPECT_NEAR(vc, 5.0, 0.01);
}

TEST(TransientRC, DischargeDecaysWithSameTau) {
    // C initially at 5 V discharging through parallel R; tau = RC = 1 s.
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::Resistor, n1, gnd, 1000.0);
    int capId = c.addComponent(ComponentType::Capacitor, n1, gnd, 1e-3);

    CircuitSolver solver;
    TransientState state;
    state.capVoltage[capId] = 5.0;

    for (int i = 0; i < 1000; ++i)
        solver.stepTransient(c, state, 1e-3);

    EXPECT_NEAR(stateCapV(state, capId), 5.0 * std::exp(-1.0), 0.02);
}

TEST(TransientRL, CurrentRisesWithTauLOverR) {
    // V=5, R=10, L=1 H -> tau = L/R = 0.1 s; final I = V/R = 0.5 A.
    RlCircuit rl = makeRlRise(5.0, 10.0, 1.0);
    CircuitSolver solver;
    TransientState state;
    Circuit circuit = rl.c;

    for (int i = 0; i < 1000; ++i)
        solver.stepTransient(circuit, state, 1e-4); // to t = tau

    EXPECT_NEAR(stateIndI(state, rl.indId), 0.5 * (1.0 - std::exp(-1.0)), 0.005);

    for (int i = 0; i < 9000; ++i)
        solver.stepTransient(circuit, state, 1e-4); // to t = 10 tau

    EXPECT_NEAR(stateIndI(state, rl.indId), 0.5, 0.002);
}

TEST(TransientEnergy, StoredCapacitorEnergyMatchesHalfCV2) {
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
    CircuitSolver solver;
    TransientState state;
    Circuit circuit = rc.c;

    double capEnergyAccumulated = 0.0; // integral of capacitor branch power
    double dt = 1e-3;
    for (int i = 0; i < 5000; ++i) {
        auto solution = solver.stepTransient(circuit, state, dt);
        const BranchResult* cap = branchFor(solution, rc.capId);
        ASSERT_NE(cap, nullptr);
        capEnergyAccumulated += cap->power * dt;
    }

    double vc = stateCapV(state, rc.capId);
    double analytic = current_lab::physics::capacitorEnergy(1e-3, vc);
    EXPECT_NEAR(capEnergyAccumulated, analytic, analytic * 0.03);
}

TEST(TransientEnergy, StoredInductorEnergyMatchesHalfLI2) {
    RlCircuit rl = makeRlRise(5.0, 10.0, 1.0);
    CircuitSolver solver;
    TransientState state;
    Circuit circuit = rl.c;

    double indEnergyAccumulated = 0.0;
    double dt = 1e-4;
    for (int i = 0; i < 10000; ++i) {
        auto solution = solver.stepTransient(circuit, state, dt);
        const BranchResult* ind = branchFor(solution, rl.indId);
        ASSERT_NE(ind, nullptr);
        indEnergyAccumulated += ind->power * dt;
    }

    double il = stateIndI(state, rl.indId);
    double analytic = current_lab::physics::inductorEnergy(1.0, il);
    EXPECT_NEAR(indEnergyAccumulated, analytic, analytic * 0.03);
}

TEST(TransientEnergy, PowerBalancesEveryStep) {
    // Tellegen: total branch power sums to zero at every solved time point
    // (supplied power is negative, dissipated + stored-rate positive).
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
    CircuitSolver solver;
    TransientState state;
    Circuit circuit = rc.c;

    for (int i = 0; i < 200; ++i) {
        auto solution = solver.stepTransient(circuit, state, 1e-3);
        double total = 0.0;
        double maxAbs = 0.0;
        for (const auto& br : solution.branches) {
            total += br.power;
            maxAbs = std::max(maxAbs, std::abs(br.power));
        }
        EXPECT_LE(std::abs(total), std::max(1e-12, maxAbs * 1e-6));
    }
}

TEST(TransientStability, BackwardEulerDoesNotBlowUpAtHugeDt) {
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3); // tau = 1 s
    CircuitSolver solver;
    TransientState state;
    Circuit circuit = rc.c;

    double prev = 0.0;
    for (int i = 0; i < 20; ++i) {
        solver.stepTransient(circuit, state, 5.0); // dt = 5 tau
        double vc = stateCapV(state, rc.capId);
        ASSERT_TRUE(std::isfinite(vc));
        EXPECT_GE(vc, prev - 1e-9);  // monotonic rise, no ringing
        EXPECT_LE(vc, 5.0 + 1e-9);   // never overshoots the source
        prev = vc;
    }
    EXPECT_NEAR(prev, 5.0, 0.01);
}

TEST(TransientConvergence, ErrorShrinksAsDtShrinks) {
    double analytic = 5.0 * (1.0 - std::exp(-1.0));
    double errors[3];
    double dts[3] = {0.1, 0.01, 0.001};
    for (int k = 0; k < 3; ++k) {
        RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
        TransientState state;
        double vc = simulateRcChargeTo(1.0, dts[k], rc, state);
        errors[k] = std::abs(vc - analytic);
    }
    EXPECT_GT(errors[0], errors[1]);
    EXPECT_GT(errors[1], errors[2]);
}

TEST(TransientConvergence, TrapezoidalBeatsBackwardEulerAtSameDt) {
    double analytic = 5.0 * (1.0 - std::exp(-1.0));
    double dt = 0.05; // tau / 20

    RcCircuit rcBe = makeRcCharge(5.0, 1000.0, 1e-3);
    TransientState beState;
    double beV = simulateRcChargeTo(1.0, dt, rcBe, beState, IntegrationMethod::BackwardEuler);

    RcCircuit rcTr = makeRcCharge(5.0, 1000.0, 1e-3);
    TransientState trState;
    double trV = simulateRcChargeTo(1.0, dt, rcTr, trState, IntegrationMethod::Trapezoidal);

    EXPECT_LT(std::abs(trV - analytic), std::abs(beV - analytic));
}

TEST(TransientState, StepAdvancesTimeAndResetClears) {
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
    CircuitSolver solver;
    TransientState state;
    Circuit circuit = rc.c;

    solver.stepTransient(circuit, state, 0.25);
    solver.stepTransient(circuit, state, 0.25);
    EXPECT_NEAR(state.time, 0.5, 1e-12);
    EXPECT_FALSE(state.capVoltage.empty());

    state.reset();
    EXPECT_DOUBLE_EQ(state.time, 0.0);
    EXPECT_TRUE(state.capVoltage.empty());
}

TEST(TransientSnapshot, HoldsStoredStateWithoutAdvancingTime) {
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
    CircuitSolver solver;
    TransientState state;
    state.capVoltage[rc.capId] = 2.0; // pretend the cap is half charged

    auto snapshot = solver.solveTransientSnapshot(rc.c, state);
    const BranchResult* cap = branchFor(snapshot, rc.capId);
    const BranchResult* res = branchFor(snapshot, rc.resId);
    ASSERT_NE(cap, nullptr);
    ASSERT_NE(res, nullptr);

    EXPECT_NEAR(cap->voltageDrop, 2.0, 1e-6);        // cap held at its stored Vc
    EXPECT_NEAR(res->current, (5.0 - 2.0) / 1000.0, 1e-9); // honest t=0+ current
    EXPECT_DOUBLE_EQ(state.time, 0.0);                // no time advance
}

TEST(DcSteadyState, CapacitorActsAsOpenCircuit) {
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
    CircuitSolver solver;
    auto solution = solver.solve(rc.c);

    const BranchResult* res = branchFor(solution, rc.resId);
    const BranchResult* cap = branchFor(solution, rc.capId);
    ASSERT_NE(res, nullptr);
    ASSERT_NE(cap, nullptr);
    EXPECT_NEAR(res->current, 0.0, 1e-9);     // no steady current through C
    EXPECT_NEAR(cap->voltageDrop, 5.0, 1e-6); // full source voltage across C
}

TEST(DcSteadyState, InductorActsAsShortCircuit) {
    RlCircuit rl = makeRlRise(5.0, 10.0, 1.0);
    CircuitSolver solver;
    auto solution = solver.solve(rl.c);

    const BranchResult* res = branchFor(solution, rl.resId);
    const BranchResult* ind = branchFor(solution, rl.indId);
    ASSERT_NE(res, nullptr);
    ASSERT_NE(ind, nullptr);
    EXPECT_NEAR(res->current, 0.5, 1e-6);    // I = V/R
    EXPECT_NEAR(ind->voltageDrop, 0.0, 1e-6); // no voltage across ideal L in DC
}

TEST(TransientCharge, ChangingCapacitancePreservesCharge) {
    // Заряжаем конденсатор C=1мФ через RC-цепь до ~5В, затем меняем ёмкость
    // на C=2мФ: заряд Q должен сохраниться, напряжение адаптироваться V=Q/C.
    RcCircuit rc = makeRcCharge(5.0, 1000.0, 1e-3);
    CircuitSolver solver;
    TransientState state;
    Circuit circuit = rc.c;

    // Полный заряд до стационара: τ=RC=1с, 10000 шагов×1мс = 10τ → V≈5.0В.
    for (int i = 0; i < 10000; ++i)
        solver.stepTransient(circuit, state, 1e-3);
    double Q_before = stateCapQ(state, rc.capId);
    double V_before = stateCapV(state, rc.capId);
    EXPECT_NEAR(V_before, 5.0, 0.02);
    EXPECT_NEAR(Q_before, 1e-3 * V_before, 1e-9);

    // Меняем ёмкость конденсатора (правка компонента в редакторе).
    Component* cap = circuit.findComponent(rc.capId);
    ASSERT_NE(cap, nullptr);
    cap->value = 2e-3; // удваиваем C

    // Один шаг с новым C: companion должен использовать vOld = Q/C_new.
    solver.stepTransient(circuit, state, 1e-6);
    double Q_after = stateCapQ(state, rc.capId);
    double V_after = stateCapV(state, rc.capId);

    // Заряд сохранился (плюс-минус крошечный ток утечки за один шаг).
    EXPECT_NEAR(Q_after, Q_before, Q_before * 1e-6);
    // Напряжение адаптировалось: V = Q / C_new ≈ V_before / 2.
    EXPECT_NEAR(V_after, Q_before / cap->value, 0.02);
    EXPECT_NEAR(V_after, V_before / 2.0, 0.02);
}

TEST(TransientAc, SignAlternatesOverFullPeriod) {
    // AC-источник (амплитуда 5 В, 50 Гц, фаза 0) должен давать
    // положительное напряжение на одной полуволне и отрицательное на другой.
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    int acId = c.addComponent(ComponentType::AcVoltageSource, n1, gnd, 5.0);
    Component* ac = c.findComponent(acId);
    ASSERT_NE(ac, nullptr);
    ac->frequency = 50.0; // T = 20 ms
    ac->phase = 0.0;
    c.addComponent(ComponentType::Resistor, n1, gnd, 1000.0); // load

    CircuitSolver solver;
    TransientState state;
    double dt = 1e-4; // 0.1 ms per step

    bool sawPositive = false, sawNegative = false;
    for (int i = 0; i < 500; ++i) { // 50 ms = 2.5 periods
        auto sol = solver.stepTransient(c, state, dt);
        double vOut = 0.0;
        for (const auto& np : sol.nodePotentials) {
            if (np.nodeId == n1) { vOut = np.potential; break; }
        }
        if (vOut > 0.5) sawPositive = true;
        if (vOut < -0.5) sawNegative = true;
    }
    EXPECT_TRUE(sawPositive) << "AC source should produce positive voltage on the positive half-cycle";
    EXPECT_TRUE(sawNegative) << "AC source should produce negative voltage on the negative half-cycle";
}

TEST(TransientAc, ReturnsAmplitudeAtQuarterPeriod) {
    // v(t) = A·sin(2π·f·t). При t = T/4 = 5 мс: v(5ms) = A·sin(π/2) = A.
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    int acId = c.addComponent(ComponentType::AcVoltageSource, n1, gnd, 5.0);
    Component* ac = c.findComponent(acId);
    ASSERT_NE(ac, nullptr);
    ac->frequency = 50.0; // T = 20 ms
    ac->phase = 0.0;
    c.addComponent(ComponentType::Resistor, n1, gnd, 1000.0);

    CircuitSolver solver;
    TransientState state;
    double dt = 5e-3; // один шаг = T/4
    auto sol = solver.stepTransient(c, state, dt);
    double vOut = 0.0;
    for (const auto& np : sol.nodePotentials)
        if (np.nodeId == n1) { vOut = np.potential; break; }
    EXPECT_NEAR(vOut, 5.0, 0.1);
}
