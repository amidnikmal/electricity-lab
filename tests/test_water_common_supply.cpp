#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <vector>
#include "circuit/Circuit.h"
#include "physics/ChannelSpecs.h"
#include "physics/DriftModel.h"
#include "physics/ParticleSim.h"
#include "solver/CircuitSolver.h"

// Инвариант «общего водопровода» (вопрос пользователя 2026-06-16):
// в водяном виде гранулы должны присутствовать И СЛЕВА, И СПРАВА контура,
// и все они принадлежат ОДНОЙ связной водяной сети (общая магистраль через
// junction-камеры), а не разрозненным изолированным карманам.
//
// Как вода привязана к сети (см. ChannelSpecs.h / ParticleSim.h):
//   makeChannelSpecs(..., waterWorld=true) ставит spec.connected = true и
//   прокладывает nodeA/nodeB у каждого канала. ParticleSim в connected-режиме
//   подрезает стенки на общих узлах и ставит junction-камеры, так что гранулы
//   физически перетекают по ВСЕЙ цепи (это и есть «один водопровод»). Связность
//   сети ОДНОЗНАЧНО определяется графом каналов по общим узлам (nodeA/nodeB).
//
// Строгий публичный API: SimParticle несёт componentId, ChannelSpec — nodeA/nodeB.
// Полностью строгая проверка «эта гранула из общей сети» = (а) граф каналов по
// общим узлам связен и (б) гранулы есть в каждом канале, плюс физическая
// проверка перетока (test_water_network: ParticlesActuallyCrossJunctions).
// Чего НЕ хватает в публичном API для абсолютной строгости: у SimParticle нет
// поля «networkId»/«originChannel», поэтому «принадлежность к сети» мы
// доказываем через componentId -> канал -> узловой граф, что эквивалентно для
// connected-плюмбинга.

using namespace current_lab::physics;

namespace {

// Прямоугольный контур: источник слева, резистор сверху, провода справа/снизу
// (как MainWindow::setupTestCircuit и makeLoop в test_water_network).
Circuit makeLoop(int& srcId, int& resId, int& wire1Id, int& wire2Id,
                 double resistance = 1000.0) {
    Circuit c;
    int gnd = c.addNode(Vec2(200, 300));
    int n1 = c.addNode(Vec2(200, 150));
    int n2 = c.addNode(Vec2(450, 150));
    int corner = c.addNode(Vec2(450, 300));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    srcId = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resId = c.addComponent(ComponentType::Resistor, n1, n2, resistance);
    wire1Id = c.addComponent(ComponentType::Wire, n2, corner, 0.0);
    wire2Id = c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
    return c;
}

void runFor(ParticleSim& sim, double seconds) {
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i)
        sim.step(1.0 / 60.0);
}

// Связность графа каналов по ОБЩИМ узлам (nodeA/nodeB): один связный кусок =
// один общий водопровод. Возвращает число компонент связности.
int channelNetworkComponents(const std::vector<ChannelSpec>& specs) {
    // Узел -> список каналов, инцидентных ему.
    std::map<int, std::vector<int>> nodeToChannels; // node -> индексы specs
    for (size_t i = 0; i < specs.size(); ++i) {
        nodeToChannels[specs[i].nodeA].push_back(static_cast<int>(i));
        nodeToChannels[specs[i].nodeB].push_back(static_cast<int>(i));
    }
    std::vector<bool> seen(specs.size(), false);
    int components = 0;
    for (size_t start = 0; start < specs.size(); ++start) {
        if (seen[start]) continue;
        ++components;
        std::queue<int> bfs;
        bfs.push(static_cast<int>(start));
        seen[start] = true;
        while (!bfs.empty()) {
            int ci = bfs.front();
            bfs.pop();
            for (int node : {specs[ci].nodeA, specs[ci].nodeB}) {
                for (int nb : nodeToChannels[node]) {
                    if (!seen[nb]) {
                        seen[nb] = true;
                        bfs.push(nb);
                    }
                }
            }
        }
    }
    return components;
}

} // namespace

// Каждый водяной канал помечен connected и подключён к узловому графу одной
// связной сети — топологическая предпосылка «общего водопровода».
TEST(WaterCommonSupply, ChannelNetworkIsSingleConnectedPlumbing) {
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);

    ASSERT_FALSE(specs.empty());
    for (const auto& spec : specs) {
        EXPECT_TRUE(spec.connected) << "канал " << spec.componentId
                                    << " не в общей сети (connected=false)";
        EXPECT_GE(spec.nodeA, 0);
        EXPECT_GE(spec.nodeB, 0);
    }
    EXPECT_EQ(channelNetworkComponents(specs), 1)
        << "вода разбита на изолированные карманы вместо одного водопровода";
}

// Гранулы присутствуют И СЛЕВА (источник/левая вертикаль), И СПРАВА (правая
// вертикаль) контура, и в КАЖДОМ канале есть вода — единый заполненный контур.
TEST(WaterCommonSupply, GrainsPresentOnBothSidesOfTheLoop) {
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);
    double radius = particleWorldRadius(8.0);

    ParticleSim sim;
    sim.configure(specs, radius);
    runFor(sim, 4.0);

    auto particles = sim.particles();
    ASSERT_FALSE(particles.empty());

    // Левая сторона контура = канал источника (x≈200); правая = провод w1
    // (правая вертикаль, x≈450). Если вода только в одной ветви — это не общая
    // магистраль, а изолированный карман.
    std::map<int, int> countByComponent;
    for (const auto& p : particles)
        ++countByComponent[p.componentId];

    EXPECT_GT(countByComponent[srcId], 0) << "нет воды в левой ветви (источник)";
    EXPECT_GT(countByComponent[w1], 0) << "нет воды в правой ветви (провод)";

    // И каждый канал заполнен — вода доходит по всей сети, а не висит локально.
    for (const auto& spec : specs)
        EXPECT_GT(countByComponent[spec.componentId], 0)
            << "канал " << spec.componentId << " пуст — вода не дошла до него";

    // Геометрический контроль «слева и справа»: разброс по X покрывает обе
    // вертикали контура (левую ~200 и правую ~450).
    double minX = 1e18, maxX = -1e18;
    for (const auto& p : particles) {
        minX = std::min(minX, p.pos.x);
        maxX = std::max(maxX, p.pos.x);
    }
    EXPECT_LT(minX, 260.0) << "нет гранул у левой стороны контура";
    EXPECT_GT(maxX, 400.0) << "нет гранул у правой стороны контура";
}

// Сильный физический прокси «один водопровод»: при работающей циркуляции
// гранулы реально перетекают между ветвями через junction-камеры, значит обе
// стороны питаются ИЗ ОДНОГО источника воды (а не из раздельных карманов).
// Полностью строгий API дал бы networkId у SimParticle; здесь доказываем через
// сохранение полного числа + перераспределение по каналам (переток).
TEST(WaterCommonSupply, BothSidesAreFedByOneCirculatingNetwork) {
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);

    ParticleSim sim;
    sim.configure(specs, particleWorldRadius(8.0));

    auto countByComponent = [&]() {
        std::map<int, int> counts;
        for (const auto& p : sim.particles())
            ++counts[p.componentId];
        return counts;
    };
    auto total = [](const std::map<int, int>& m) {
        int s = 0;
        for (const auto& [id, n] : m) s += n;
        return s;
    };

    auto before = countByComponent();
    int totalBefore = total(before);
    ASSERT_GT(totalBefore, 0);

    runFor(sim, 4.0);
    auto after = countByComponent();

    // Вода не создаётся и не исчезает — одна замкнутая сеть.
    EXPECT_EQ(totalBefore, total(after)) << "вода создаётся/исчезает — не одна сеть";

    // Между ветвями произошёл переток через камеры (изолированные карманы так
    // не делают): хотя бы один канал изменил населённость.
    bool anyChanged = false;
    for (const auto& [id, n] : after)
        if (n != before[id]) anyChanged = true;
    EXPECT_TRUE(anyChanged)
        << "ни одна гранула не пересекла junction — ветви изолированы";

    // И обе стороны по-прежнему населены — общий водопровод не осушил одну ветвь.
    EXPECT_GT(after[srcId], 0) << "левая ветвь осушена";
    EXPECT_GT(after[w1], 0) << "правая ветвь осушена";
}
