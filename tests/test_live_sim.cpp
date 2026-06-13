#include <gtest/gtest.h>
#include <cmath>
#include "circuit/Circuit.h"
#include "simulation/LiveSim.h"
#include "solver/CircuitSolver.h"

// Слитый live-режим (LiveSim): цепь всегда живёт во времени, «DC steady» —
// предел процесса. Контракты: резистивная цепь стационарна сразу; RC честно
// заряжается в авто-замедлении и засыпает РОВНО на DC-асимптоте; события
// будят, не стирая заряд; ~solveHz решений на реальную секунду.
namespace {

using namespace current_lab::simulation;

// Источник 5 В — последовательный Rs — ключ — (Rp || C). Ключ закрыт: C
// заряжается к 5*2/3 В с tau=(Rs||Rp)*C; открыт: разряд через Rp.
struct SwitchedRc {
    Circuit circuit;
    int srcId = -1, rsId = -1, switchId = -1, rpId = -1, capId = -1;
};

SwitchedRc makeSwitchedRc(bool closed) {
    SwitchedRc s;
    Circuit& c = s.circuit;
    int gnd = c.addNode(Vec2(0, 200), "GND");
    int n1 = c.addNode(Vec2(0, 0), "N1");
    int n2 = c.addNode(Vec2(120, 0), "N2");
    int n3 = c.addNode(Vec2(240, 0), "N3");
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    s.srcId = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    s.rsId = c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    s.switchId = c.addComponent(ComponentType::Switch, n2, n3, closed ? 1.0 : 0.0);
    s.rpId = c.addComponent(ComponentType::Resistor, n3, gnd, 2000.0);
    s.capId = c.addComponent(ComponentType::Capacitor, n3, gnd, 1e-6);
    return s;
}

Circuit makeResistiveLoop(int& resId) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100), "GND");
    int n1 = c.addNode(Vec2(0, 0), "N1");
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resId = c.addComponent(ComponentType::Resistor, n1, gnd, 1000.0);
    return c;
}

double branchCurrent(const CircuitSolution& sol, int compId) {
    for (const auto& br : sol.branches)
        if (br.componentId == compId) return br.current;
    return 0.0;
}

// Гоняет кадры по 1/60 реальной секунды до засыпания (или до лимита).
int advanceUntilSettled(LiveSim& sim, const Circuit& c, CircuitSolver& solver,
                        CircuitSolution& sol, int maxFrames = 5000) {
    int frames = 0;
    while (!sim.settled() && frames < maxFrames) {
        sim.advance(c, solver, 1.0 / 60.0, sol);
        ++frames;
    }
    return frames;
}

} // namespace

TEST(LiveSim, ResistiveCircuitSettlesImmediatelyToDc) {
    int resId = -1;
    Circuit c = makeResistiveLoop(resId);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;

    sim.onCircuitEvent(c, solver);
    EXPECT_FALSE(sim.settled());
    EXPECT_TRUE(sim.advance(c, solver, 1.0 / 60.0, sol));
    EXPECT_TRUE(sim.settled()) << "цепь без C/L стационарна после первого же шага";

    CircuitSolution dc = solver.solve(c);
    EXPECT_NEAR(branchCurrent(sol, resId), branchCurrent(dc, resId), 1e-9)
        << "уснувшее решение обязано совпадать с DC-оракулом";
    EXPECT_NEAR(branchCurrent(sol, resId), 0.005, 1e-6);

    // Спим — решений больше нет.
    EXPECT_FALSE(sim.advance(c, solver, 1.0, sol));
}

TEST(LiveSim, TheveninProbeRecoversSeriesResistance) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/true);
    CircuitSolver solver;
    // C видит Rs || Rp = 666.7 Ом (идеальный источник — нулевое внутреннее).
    double rth = theveninResistanceSeenBy(s.circuit, s.capId, solver);
    EXPECT_NEAR(rth, 1000.0 * 2000.0 / 3000.0, 5.0);

    LiveSimConfig cfg;
    double tau = smallestTimeConstant(s.circuit, solver, cfg);
    EXPECT_NEAR(tau, 666.7 * 1e-6, 5e-6);
}

TEST(LiveSim, OpenSwitchAroundSourceStillGivesDischargeTau) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/false);
    CircuitSolver solver;
    // Ключ разомкнут: C видит только Rp -> tau разряда = Rp*C = 2 мс.
    double rth = theveninResistanceSeenBy(s.circuit, s.capId, solver);
    EXPECT_NEAR(rth, 2000.0, 10.0);
}

TEST(LiveSim, AutoSpeedIsHeavySlowMotionFromTau) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/true);
    CircuitSolver solver;
    LiveSim sim;
    sim.onCircuitEvent(s.circuit, solver);
    // 3*tau (2 мс) растягиваются на storySeconds (2.5 с): скорость ~ 8e-4.
    LiveSimConfig cfg;
    double tau = smallestTimeConstant(s.circuit, solver, cfg);
    EXPECT_NEAR(sim.simSpeed(), 3.0 * tau / cfg.storySeconds, 1e-5);
    EXPECT_LT(sim.simSpeed(), 0.01) << "транзиент обязан идти в сильном замедлении";
    EXPECT_TRUE(sim.autoSpeed());

    sim.setManualSpeed(0.5);
    EXPECT_FALSE(sim.autoSpeed());
    EXPECT_NEAR(sim.simSpeed(), 0.5, 1e-12);
    EXPECT_NEAR(sim.dt(), 0.5 / 60.0, 1e-9);
    sim.setAutoSpeed();
    EXPECT_TRUE(sim.autoSpeed());
}

TEST(LiveSim, RcChargesMonotonicallyThenSleepsOnExactAsymptote) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/true);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;
    sim.onCircuitEvent(s.circuit, solver);

    double prevVc = -1.0;
    int frames = 0;
    while (!sim.settled() && frames < 5000) {
        if (sim.advance(s.circuit, solver, 1.0 / 60.0, sol)) {
            auto it = sim.state().capVoltage.find(s.capId);
            if (it != sim.state().capVoltage.end()) {
                EXPECT_GE(it->second, prevVc - 1e-9) << "заряд обязан расти монотонно";
                prevVc = it->second;
            }
        }
        ++frames;
    }
    ASSERT_TRUE(sim.settled()) << "RC обязан устаканиться, кадров: " << frames;
    EXPECT_GT(frames, 30) << "процесс не должен схлопнуться мгновенно (slow-mo)";

    // Снап на точную асимптоту: Vc = 5*2/3, ток конденсатора ~0.
    CircuitSolution dc = solver.solve(s.circuit);
    EXPECT_NEAR(sim.state().capVoltage.at(s.capId), 5.0 * 2.0 / 3.0, 1e-3);
    EXPECT_NEAR(branchCurrent(sol, s.capId), branchCurrent(dc, s.capId), 1e-9);
    EXPECT_NEAR(std::abs(branchCurrent(sol, s.capId)), 0.0, 1e-6);
}

TEST(LiveSim, SwitchOpenDischargesAndSettlesAtZero) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/true);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;

    sim.onCircuitEvent(s.circuit, solver);
    advanceUntilSettled(sim, s.circuit, solver, sol);
    ASSERT_TRUE(sim.settled());
    double charged = sim.state().capVoltage.at(s.capId);
    ASSERT_GT(charged, 3.0);

    // Щелчок ключа: размыкаем. Заряд сохраняется, симуляция просыпается.
    int idx = s.circuit.componentIndex(s.switchId);
    ASSERT_GE(idx, 0);
    s.circuit.components[idx].value = 0.0;
    sim.onCircuitEvent(s.circuit, solver);
    EXPECT_FALSE(sim.settled());
    EXPECT_NEAR(sim.state().capVoltage.at(s.capId), charged, 1e-12)
        << "событие не имеет права стирать заряд";

    // Медленная остановка: разряд через Rp до нуля, затем сон.
    advanceUntilSettled(sim, s.circuit, solver, sol);
    ASSERT_TRUE(sim.settled());
    EXPECT_NEAR(sim.state().capVoltage.at(s.capId), 0.0, 1e-3);
    EXPECT_NEAR(branchCurrent(sol, s.rpId), 0.0, 1e-6);
}

TEST(LiveSim, SolveRateMatchesConfiguredHz) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/true);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;
    sim.onCircuitEvent(s.circuit, solver);

    // Секунда реального времени кадрами по 1/60: симулированное время должно
    // составить speed*1.0, т.е. ровно ~solveHz шагов по dt (не 120 и не 2000).
    for (int i = 0; i < 60 && !sim.settled(); ++i)
        sim.advance(s.circuit, solver, 1.0 / 60.0, sol);
    double expected = sim.simSpeed() * 1.0;
    EXPECT_NEAR(sim.time(), expected, expected * 0.05);
    EXPECT_NEAR(sim.time() / sim.dt(), 60.0, 3.0);
}

TEST(LiveSim, DischargeZeroesStateAndWakes) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/true);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;
    sim.onCircuitEvent(s.circuit, solver);
    advanceUntilSettled(sim, s.circuit, solver, sol);
    ASSERT_GT(sim.state().capVoltage.at(s.capId), 3.0);

    sim.discharge();
    EXPECT_FALSE(sim.settled());
    EXPECT_TRUE(sim.state().capVoltage.empty());
    EXPECT_EQ(sim.time(), 0.0);
}

TEST(LiveSim, CurrentSolutionMatchesSleepState) {
    int resId = -1;
    Circuit c = makeResistiveLoop(resId);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;
    sim.onCircuitEvent(c, solver);
    EXPECT_NEAR(branchCurrent(sim.currentSolution(c, solver), resId), 0.005, 1e-6)
        << "до первого шага — честный снапшот текущего состояния";
    sim.advance(c, solver, 1.0 / 60.0, sol);
    ASSERT_TRUE(sim.settled());
    EXPECT_NEAR(branchCurrent(sim.currentSolution(c, solver), resId), 0.005, 1e-6)
        << "во сне — DC-асимптота";
}

// ─── Регрессии adversarial-ревью 2026-06-12 ────────────────────────────────

namespace {

// ТОПОЛОГИЯ ДЕМКИ SwitchedRc: без шунта через конденсатор — за разомкнутым
// ключом у конденсатора НЕТ DC-пути (ловушка gmin-делителя).
struct DemoRc {
    Circuit circuit;
    int switchId = -1, capId = -1;
};

DemoRc makeDemoLikeRc(bool closed, double cap = 1e-6) {
    DemoRc d;
    Circuit& c = d.circuit;
    int gnd = c.addNode(Vec2(0, 200), "GND");
    int n1 = c.addNode(Vec2(0, 0), "N1");
    int n2 = c.addNode(Vec2(120, 0), "N2");
    int n3 = c.addNode(Vec2(240, 0), "N3");
    int corner = c.addNode(Vec2(240, 200));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    d.switchId = c.addComponent(ComponentType::Switch, n1, n2, closed ? 1.0 : 0.0);
    c.addComponent(ComponentType::Resistor, n2, n3, 1000.0);
    d.capId = c.addComponent(ComponentType::Capacitor, n3, corner, cap);
    c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
    return d;
}

void setSwitch(Circuit& c, int switchId, bool closed) {
    int idx = c.componentIndex(switchId);
    ASSERT_GE(idx, 0);
    c.components[idx].value = closed ? 1.0 : 0.0;
}

} // namespace

// КРИТИКА ревью (подтверждена численно): solve() рисует фиктивный делитель из
// gmin-утечек, и снап на «асимптоту» приписывал конденсатору за никогда не
// замыкавшимся ключом половину питания, а при размыкании — стирал заряд.
TEST(LiveSim, IsolatedCapacitorKeepsChargeWhileSleeping) {
    DemoRc d = makeDemoLikeRc(/*closed=*/false);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;

    // (а) Загрузка разряженной демки: за разомкнутым ключом Vc обязан
    // остаться нулём, а не стать V/2 от делителя утечек.
    sim.onCircuitEvent(d.circuit, solver);
    advanceUntilSettled(sim, d.circuit, solver, sol);
    ASSERT_TRUE(sim.settled());
    auto it = sim.state().capVoltage.find(d.capId);
    if (it != sim.state().capVoltage.end())
        EXPECT_NEAR(it->second, 0.0, 1e-3)
            << "фантомный заряд от gmin-делителя за разомкнутым ключом";

    // (б) Зарядили при замкнутом ключе, разомкнули — заряд ОБЯЗАН пережить сон.
    setSwitch(d.circuit, d.switchId, true);
    sim.onCircuitEvent(d.circuit, solver);
    advanceUntilSettled(sim, d.circuit, solver, sol);
    ASSERT_TRUE(sim.settled());
    double charged = sim.state().capVoltage.at(d.capId);
    ASSERT_NEAR(charged, 5.0, 1e-3);

    setSwitch(d.circuit, d.switchId, false);
    sim.onCircuitEvent(d.circuit, solver);
    EXPECT_NEAR(sim.state().capVoltage.at(d.capId), charged, 1e-12)
        << "событие не имеет права трогать заряд";
    advanceUntilSettled(sim, d.circuit, solver, sol);
    ASSERT_TRUE(sim.settled());
    EXPECT_NEAR(sim.state().capVoltage.at(d.capId), charged, 1e-2)
        << "сон не имеет права стирать заряд изолированного конденсатора";
}

// МАЖОР ревью: wake() обнулял аккумулятор — события каждый кадр (ручка,
// drag) на 120 Гц замораживали время цепи насовсем.
TEST(LiveSim, PerFrameEventsDoNotStarveTheIntegrator) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/true);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;
    sim.onCircuitEvent(s.circuit, solver);
    double speed = sim.simSpeed();

    // 2 реальные секунды кадрами по 1/120, событие КАЖДЫЙ кадр.
    for (int i = 0; i < 240; ++i) {
        sim.onCircuitEvent(s.circuit, solver);
        sim.advance(s.circuit, solver, 1.0 / 120.0, sol);
    }
    EXPECT_GT(sim.time(), 0.5 * speed * 2.0)
        << "интегратор голодает: события стирают вклад неполных кадров";
}

// Телепорт при глубоком ручном замедлении: шаговый порог тишины ловил
// «затухание» посреди процесса и прыгал к асимптоте. Скоростной критерий
// (|dV|/dt) от замедления не зависит.
TEST(LiveSim, DeepManualSlowMoDoesNotFalseSettle) {
    DemoRc d = makeDemoLikeRc(/*closed=*/true, /*cap=*/1e-3); // tau ~ 0.67 s
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;
    sim.onCircuitEvent(d.circuit, solver);
    sim.setManualSpeed(4e-6);

    for (int i = 0; i < 60; ++i)
        sim.advance(d.circuit, solver, 1.0 / 60.0, sol);

    EXPECT_FALSE(sim.settled())
        << "ложное засыпание в начале процесса при глубоком slow-mo";
    auto it = sim.state().capVoltage.find(d.capId);
    double vc = it != sim.state().capVoltage.end() ? it->second : 0.0;
    EXPECT_LT(vc, 0.1) << "телепорт заряда к асимптоте";
}

// Дубли id в toDistributed: провод раньше конденсатора раздавал сегментам
// уже занятые id — тевенин-проба подменяла сегмент вместо конденсатора.
TEST(LiveSim, DistributedWiresKeepUniqueIdsAndTau) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 200), "GND");
    int n1 = c.addNode(Vec2(0, 0), "N1");
    int n2 = c.addNode(Vec2(120, 0), "N2");
    int n3 = c.addNode(Vec2(240, 0), "N3");
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::Wire, n2, n3, 0.0); // провод РАНЬШЕ остальных
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    int capId = c.addComponent(ComponentType::Capacitor, n3, gnd, 1e-6);

    Circuit dist = c.toDistributed(8);
    std::set<int> ids;
    for (const auto& comp : dist.components)
        EXPECT_TRUE(ids.insert(comp.id).second)
            << "дубль component id " << comp.id << " в распределённой цепи";

    CircuitSolver solver;
    LiveSimConfig cfg;
    double tauLumped = smallestTimeConstant(c, solver, cfg);
    double tauDist = smallestTimeConstant(dist, solver, cfg);
    ASSERT_GT(tauLumped, 0.0);
    ASSERT_GT(tauDist, 0.0) << "проба подменила сегмент провода вместо C";
    EXPECT_NEAR(tauDist, tauLumped, tauLumped * 0.25);
    (void)capId;
}

// Кнопка Step: будит из сна, шагает ровно на dt и снова засыпает на той же
// асимптоте (заряд не трогает).
TEST(LiveSim, StepOnceWakesAndResettlesOnSameAsymptote) {
    SwitchedRc s = makeSwitchedRc(/*closed=*/true);
    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;
    sim.onCircuitEvent(s.circuit, solver);
    advanceUntilSettled(sim, s.circuit, solver, sol);
    ASSERT_TRUE(sim.settled());
    double vc = sim.state().capVoltage.at(s.capId);
    double t0 = sim.time();

    sim.stepOnce(s.circuit, solver, sol);
    EXPECT_NEAR(sim.time(), t0 + sim.dt(), 1e-12);
    EXPECT_NEAR(sim.state().capVoltage.at(s.capId), vc, 1e-6);

    advanceUntilSettled(sim, s.circuit, solver, sol);
    EXPECT_TRUE(sim.settled());
    EXPECT_NEAR(sim.state().capVoltage.at(s.capId), vc, 1e-6);
}
