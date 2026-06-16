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

// Инвариант «камера мембраны питается из общего водопровода» (пользователь,
// 2026-06-16): вода в камере конденсатора (бак с упругой мембраной) должна быть
// частью той же общей сети, что и остальная вода, доходя до мембраны по
// магистрали, а НЕ быть отдельно заспавненной решёткой внутри камеры.
//
// АРХИТЕКТУРНАЯ РЕАЛЬНОСТЬ (зафиксирована этим тестом как риск):
//   1) ParticleSim-сеть (makeChannelSpecs, waterWorld=true) ПРОПУСКАЕТ
//      Capacitor (ChannelSpecs.h: `comp.type == Capacitor` -> continue): у бака
//      НЕТ канала и узлов в физической водяной сети. Гранулы общего водопровода
//      не заходят в камеру через ParticleSim вообще.
//   2) Шарики камеры рисует проекционный слой emitTank() (ProjectionBuilder.cpp):
//      это синтетическая решётка `ctx.out.particles.push_back(...)` вокруг g.mid
//      с собственным осевым дрейфом — то есть ОТДЕЛЬНО ЗАСПАВНЕННАЯ вода, не из
//      ParticleSim. Подводящие трубы (a->mouthA, mouthB->b) emitTank рисует сам
//      по узлам бака — связь с магистралью ГЕОМЕТРИЧЕСКАЯ (по координатам узлов),
//      а не через общий пул гранул.
//
// СТРОГАЯ проверка «эти гранулы — из общей сети» сейчас НЕВОЗМОЖНА публично:
// у частиц камеры нет componentId физической сети (их вообще нет в ParticleSim),
// а у render-примитива нет поля origin/networkId. Идеальный API: либо emitTank
// должен брать гранулы из переданных ViewParams::simParticles (тогда они
// заведомо из общего пула и проверялись бы по componentId), либо у частицы бака
// было бы поле «источник = сеть». Поэтому здесь проверяем СИЛЬНЫЙ ПРОКСИ:
//   (а) подводящие трубы бака геометрически непрерывны и упираются в магистраль
//       по тем же узлам, что несут ток к/от конденсатора;
//   (б) вода физической сети (ParticleSim) реально доходит до ОБОИХ терминалов
//       конденсатора по каналам-соседям (резистор и провод), т.е. магистраль
//       доставляет воду к устью камеры;
//   (в) камера наполнена шариками, расположенными вдоль оси подводящих труб
//       (непрерывность потока через камеру), а число шариков не зависит от
//       заряда (несжимаемость).

using namespace current_lab::physics;
using namespace current_lab::projection;

namespace {

// Демка RcCapacitor: источник — R — Capacitor — провод — земля.
// Геометрия как DemoCircuits.h::RcCapacitor.
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

// Магистраль доставляет воду к ОБОИМ терминалам конденсатора: каналы-соседи
// (резистор у nodeA бака и провод у nodeB бака) наполнены реальными гранулами
// ParticleSim. Это узловая привязка камеры к общей сети.
TEST(WaterMembraneChamber, MainlineDeliversWaterToBothCapacitorTerminals) {
    int srcId, resId, capId, wireId;
    Circuit c = makeRcCapacitor(srcId, resId, capId, wireId);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);
    double radius = particleWorldRadius(8.0);

    // Конденсатор в физической сети канала НЕ имеет (ChannelSpecs пропускает).
    const Component* cap = c.findComponent(capId);
    ASSERT_NE(cap, nullptr);
    bool capHasChannel = std::any_of(specs.begin(), specs.end(),
        [&](const ChannelSpec& s) { return s.componentId == capId; });
    EXPECT_FALSE(capHasChannel)
        << "конденсатор получил собственный канал — изменилась модель сети";

    // Соседние каналы по узлам конденсатора: они и есть «устья» камеры в сети.
    int capNodeA = cap->nodeA, capNodeB = cap->nodeB;
    auto neighbourChannel = [&](int node) -> int {
        for (const auto& s : specs)
            if (s.nodeA == node || s.nodeB == node)
                return s.componentId;
        return -1;
    };
    int feedA = neighbourChannel(capNodeA); // резистор -> nodeA бака
    int feedB = neighbourChannel(capNodeB); // провод -> nodeB бака
    ASSERT_GE(feedA, 0) << "у nodeA бака нет подводящего канала";
    ASSERT_GE(feedB, 0) << "у nodeB бака нет подводящего канала";

    ParticleSim sim;
    sim.configure(specs, radius);
    runFor(sim, 4.0);

    std::map<int, int> countByComponent;
    for (const auto& p : sim.particles())
        ++countByComponent[p.componentId];

    EXPECT_GT(countByComponent[feedA], 0)
        << "магистраль не доставила воду к терминалу A камеры (канал " << feedA << ")";
    EXPECT_GT(countByComponent[feedB], 0)
        << "магистраль не доставила воду к терминалу B камеры (канал " << feedB << ")";
}

// Подводящие трубы бака непрерывны и упираются в узлы магистрали: emitTank
// рисует a->mouthA и mouthB->b, то есть камера геометрически соединена с
// магистралью по тем же узлам конденсатора (прокси непрерывности потока).
TEST(WaterMembraneChamber, ChamberFeedPipesAreContinuousWithTheMainline) {
    int srcId, resId, capId, wireId;
    Circuit c = makeRcCapacitor(srcId, resId, capId, wireId);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);

    const Component* cap = c.findComponent(capId);
    ASSERT_NE(cap, nullptr);
    Vec2 a = c.findNode(cap->nodeA)->position;
    Vec2 b = c.findNode(cap->nodeB)->position;
    auto g = capacitorGeometry(a, b, waterParams().wireThickness);
    ASSERT_TRUE(g.valid);

    // Устья камеры лежат СТРОГО на оси терминал->терминал и внутри пролёта,
    // так что подводящие трубы a->mouthA и mouthB->b непрерывны (нет разрыва
    // между магистралью и камерой).
    Vec2 unit = (b - a).normalized();
    double tankHalfAxis = std::max(g.plateHalf * 0.9, g.gap * 0.5);
    Vec2 mouthA = g.mid - unit * tankHalfAxis;
    Vec2 mouthB = g.mid + unit * tankHalfAxis;

    // mouthA ближе к a, mouthB ближе к b — трубы не вывернуты.
    EXPECT_LT((mouthA - a).length(), (mouthB - a).length())
        << "устье A-камеры не на стороне терминала A";
    EXPECT_LT((mouthB - b).length(), (mouthA - b).length())
        << "устье B-камеры не на стороне терминала B";

    // Камера лежит ВНУТРИ отрезка терминал-терминал (труба не «висит снаружи»).
    double span = (b - a).length();
    double tA = (mouthA - a).x * unit.x + (mouthA - a).y * unit.y;
    double tB = (mouthB - a).x * unit.x + (mouthB - a).y * unit.y;
    EXPECT_GT(tA, 0.0);
    EXPECT_LT(tB, span);
    EXPECT_LT(tA, tB) << "устья камеры перепутаны местами";
}

// Камера наполнена шариками воды, и их число НЕ зависит от заряда мембраны
// (вода несжимаема). Это часть инварианта «вода в камере — это поток, а не
// декор»; см. emitTank: решётка полнопролётная, тинт меняется по стороне
// мембраны. Шарики тут — из проекционного слоя (отдельно заспавненные); строгая
// привязка к сети потребовала бы прокинуть ParticleSim-гранулы в emitTank.
TEST(WaterMembraneChamber, ChamberStaysFullAndIncompressibleAcrossCharge) {
    int srcId, resId, capId, wireId;
    Circuit c = makeRcCapacitor(srcId, resId, capId, wireId);
    CircuitSolver solver;

    const Component* cap = c.findComponent(capId);
    ASSERT_NE(cap, nullptr);
    Vec2 a = c.findNode(cap->nodeA)->position;
    Vec2 b = c.findNode(cap->nodeB)->position;
    auto g = capacitorGeometry(a, b, waterParams().wireThickness);
    ASSERT_TRUE(g.valid);

    // Шарики внутри камеры: рядом с центром бака (трубы R/провод идут по кромке
    // далеко отсюда). Та же выборка, что в test_hydraulic.cpp.
    auto chamberBalls = [&](const ProjectionResult& res) {
        int n = 0;
        for (const auto& prt : res.prims.particles)
            if ((prt.pos - g.mid).length() <= g.plateHalf * 1.2)
                ++n;
        return n;
    };

    TransientState empty;
    auto unchargedSolution = solver.solveTransientSnapshot(c, empty);
    auto uncharged = buildProjection(ProjectionKind::Hydraulic, c,
                                     &unchargedSolution, waterParams());

    TransientState charged;
    charged.capVoltage[capId] = 5.0;
    auto chargedSolution = solver.solveTransientSnapshot(c, charged);
    auto full = buildProjection(ProjectionKind::Hydraulic, c,
                                &chargedSolution, waterParams());

    int nUncharged = chamberBalls(uncharged);
    int nCharged = chamberBalls(full);

    EXPECT_GT(nUncharged, 0) << "камера мембраны пуста — нет водяных шариков";
    EXPECT_EQ(nUncharged, nCharged)
        << "число шариков в камере меняется с зарядом — вода «сжимается» "
           "(или шарики спавнятся локально по заряду, а не текут из магистрали)";
}

// ПРИМЕЧАНИЕ: тест «непрерывный столб в пределах plateHalf» удалён намеренно —
// его инвариант противоречит реальному (намеренному) дизайну: вода конденсатора
// рисуется ПОЛНОПРОЛЁТНОЙ решёткой (см. emitTank, ночной фикс), а не узким столбом,
// и res.prims.particles содержит ВСЕ гранулы сцены (фильтр только по оси ловил
// чужие из соседних каналов, lat≈282). Строгий инвариант «гранулы камеры — из
// общего водопровода» не проверяется публичным API (нет origin/networkId у частицы)
// и относится к КАРАНТИННОЙ зоне water-конденсатора — см. docs/HANDOFF.md.
