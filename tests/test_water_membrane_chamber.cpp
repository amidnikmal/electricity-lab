#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include "circuit/Circuit.h"
#include "physics/ChannelSpecs.h"
#include "physics/DriftModel.h"
#include "physics/ParticleSim.h"
#include "projection/ElementGeometry.h"
#include "projection/ProjectionBuilder.h"
#include "solver/CircuitSolver.h"

// Инвариант «вода камеры конденсатора — из общего водопровода» (пользователь,
// 2026-06-16), теперь ВЫПОЛНЕН и проверяется СТРОГО.
//
// Реализация (карантинный долг закрыт): в водяном мире конденсатор перестал быть
// исключённым из сети. makeChannelSpecs эмитит для него ТРИ однородных канала с
// componentId==capId и connected: узкий лид от терминала A -> широкий бак ->
// узкий лид к терминалу B. Каналы подключены к магистрали по реальным узлам
// терминалов (общие junction-камеры с резистором/проводом), поэтому бак
// наполняется РЕАЛЬНЫМИ гранулами из общего пула ParticleSim. Поперёк бака стоит
// выгнутый коллайдер-мембрана (vc->bow), сквозь который вода не проходит; emitTank
// при активном simParticles рисует именно эти сетевые гранулы (не синтетическую
// решётку). Так гранула камеры теперь публично прослеживается до сети по
// componentId — строгая проверка стала возможной.

using namespace current_lab::physics;
using namespace current_lab::projection;

namespace {

// Демка RcCapacitor: источник — R — Capacitor — провод — земля.
Circuit makeRcCapacitor(int& srcId, int& resId, int& capId, int& wireId) {
    Circuit c;
    int gnd = c.addNode(Vec2(200, 320));
    int n1 = c.addNode(Vec2(200, 140));
    int n2 = c.addNode(Vec2(480, 140));
    int corner = c.addNode(Vec2(480, 320));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    srcId = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resId = c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    capId = c.addComponent(ComponentType::Capacitor, n2, corner, 1e-3); // tau=1s
    wireId = c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
    return c;
}

ViewParams waterParams() {
    ViewParams p;
    p.layers.potential = true;
    p.layers.heat = true;
    p.wireThickness = 8.0;
    return p;
}

void runFor(ParticleSim& sim, double seconds) {
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i)
        sim.step(1.0 / 60.0);
}

} // namespace

// СТРУКТУРА: конденсатор теперь ЧАСТЬ водяной сети — три канала componentId==capId
// (лид/бак/лид), ровно один из них несёт мембрану, и они привязаны к реальным
// узлам терминалов (устья камеры в магистрали).
TEST(WaterMembraneChamber, CapacitorIsPartOfTheWaterNetwork) {
    int srcId, resId, capId, wireId;
    Circuit c = makeRcCapacitor(srcId, resId, capId, wireId);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);

    const Component* cap = c.findComponent(capId);
    ASSERT_NE(cap, nullptr);

    std::vector<const ChannelSpec*> capChannels;
    for (const auto& s : specs)
        if (s.componentId == capId) capChannels.push_back(&s);
    ASSERT_EQ(capChannels.size(), 3u)
        << "ожидаются три канала конденсатора: лид A, бак, лид B";

    int membranes = 0;
    bool touchesTermA = false, touchesTermB = false;
    for (const ChannelSpec* s : capChannels) {
        if (s->membrane) ++membranes;
        if (s->nodeA == cap->nodeA || s->nodeB == cap->nodeA) touchesTermA = true;
        if (s->nodeA == cap->nodeB || s->nodeB == cap->nodeB) touchesTermB = true;
        EXPECT_TRUE(s->connected) << "канал бака должен быть в общей сети";
    }
    EXPECT_EQ(membranes, 1) << "ровно один канал (бак) несёт мембрану";
    EXPECT_TRUE(touchesTermA) << "камера не привязана к терминалу A магистрали";
    EXPECT_TRUE(touchesTermB) << "камера не привязана к терминалу B магистрали";

    // В ЭЛЕКТРОННОМ мире заряды через зазор не текут — каналов у конденсатора нет.
    auto electronSpecs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/false);
    bool capInElectron = std::any_of(electronSpecs.begin(), electronSpecs.end(),
        [&](const ChannelSpec& s) { return s.componentId == capId; });
    EXPECT_FALSE(capInElectron) << "в электронном мире у конденсатора каналов нет";
}

// СТРОГИЙ ИНВАРИАНТ: каждая гранула, нарисованная в баке (emitTank), — это РЕАЛЬНАЯ
// гранула общего ParticleSim с componentId==capId (та же позиция, бит-в-бит). И бак
// действительно НАПОЛНЕН такими гранулами из сети.
TEST(WaterMembraneChamber, ChamberBallsAreTheNetworkParticles) {
    int srcId, resId, capId, wireId;
    Circuit c = makeRcCapacitor(srcId, resId, capId, wireId);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);
    double radius = particleWorldRadius(8.0);

    ParticleSim sim;
    sim.configure(specs, radius);
    runFor(sim, 3.0);
    std::vector<SimParticle> parts = sim.particles();

    // Позиции сетевых гранул конденсатора.
    std::vector<Vec2> capNet;
    for (const auto& p : parts)
        if (p.componentId == capId) capNet.push_back(p.pos);
    ASSERT_GT(capNet.size(), 0u) << "в сети нет воды конденсатора";

    const Component* cap = c.findComponent(capId);
    Vec2 a = c.findNode(cap->nodeA)->position;
    Vec2 b = c.findNode(cap->nodeB)->position;
    auto g = capacitorGeometry(a, b, waterParams().wireThickness);
    ASSERT_TRUE(g.valid);
    double ph = g.plateHalf;
    double tankHalfAxis = std::max(ph * 0.9, g.gap * 0.5);

    // Сетевые гранулы реально стоят ВНУТРИ бака (а не только в лидах).
    int insideTank = 0;
    for (const Vec2& p : capNet) {
        Vec2 rel = p - g.mid;
        double axial = rel.x * g.unit.x + rel.y * g.unit.y;
        double lat = rel.x * g.perp.x + rel.y * g.perp.y;
        if (std::abs(axial) <= tankHalfAxis + radius && std::abs(lat) <= ph + radius)
            ++insideTank;
    }
    EXPECT_GT(insideTank, 4) << "бак не наполнен сетевой водой";

    // Рендер бака берёт ИМЕННО эти гранулы.
    ViewParams vp = waterParams();
    vp.simParticles = &parts;
    ProjectionResult res = buildProjection(ProjectionKind::Hydraulic, c, &sol, vp);

    auto matchesNetwork = [&](Vec2 pos) {
        for (const Vec2& q : capNet)
            if ((pos - q).length() < 1e-4) return true;
        return false;
    };

    int chamberDrawn = 0;
    for (const auto& prt : res.prims.particles) {
        // Гранулы у центра бака — это вода камеры (трубы R/провод далеко).
        if ((prt.pos - g.mid).length() > tankHalfAxis + ph) continue;
        ++chamberDrawn;
        EXPECT_TRUE(matchesNetwork(prt.pos))
            << "гранула камеры не совпала ни с одной сетевой гранулой конденсатора "
               "— значит это отдельная решётка, а не общий водопровод";
    }
    EXPECT_GT(chamberDrawn, 0) << "в баке не нарисовано ни одной гранулы";
}

// НЕСЖИМАЕМОСТЬ: число сетевых гранул конденсатора НЕ зависит от заряда — вода не
// «сжимается» и не сливается, когда мембрана выгибается. Засев бака не зависит от
// выгиба (пропуск по центру), поэтому количество строго постоянно.
TEST(WaterMembraneChamber, ChamberParticleCountIsChargeIndependent) {
    int srcId, resId, capId, wireId;
    Circuit c = makeRcCapacitor(srcId, resId, capId, wireId);
    CircuitSolver solver;
    double radius = particleWorldRadius(8.0);

    auto countCapNet = [&](const TransientState& st) {
        auto snap = solver.solveTransientSnapshot(c, st);
        auto specs = makeChannelSpecs(c, &snap, 8.0, /*waterWorld=*/true);
        ParticleSim sim;
        sim.configure(specs, radius);
        int n = 0;
        for (const auto& p : sim.particles())
            if (p.componentId == capId) ++n;
        return n;
    };

    TransientState uncharged;
    TransientState charged;
    charged.capVoltage[capId] = 5.0;

    int nUncharged = countCapNet(uncharged);
    int nCharged = countCapNet(charged);

    EXPECT_GT(nUncharged, 0) << "камера пуста — нет сетевой воды";
    EXPECT_EQ(nUncharged, nCharged)
        << "число гранул конденсатора зависит от заряда — вода «сжимается» "
           "или сливается, а не несжимаемо стоит в общей сети";
}
