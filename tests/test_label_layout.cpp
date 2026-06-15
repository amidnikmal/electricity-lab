// Жёсткий тест: надписи на холсте НЕ должны перекрываться.
//  1) declutterVertical разводит любой набор боксов до нуля перекрытий (только сдвиг вниз).
//  2) на ВСЕХ демо-схемах после раскладки перекрытий нет (с включёнными показаниями+debug,
//     то есть в самом плотном по надписям режиме).
#include <gtest/gtest.h>
#include "render/LabelLayout.h"
#include "render/CaptureHelpers.h"
#include "ui/CanvasCamera.h"
#include "projection/ProjectionBuilder.h"
#include "circuit/DemoCircuits.h"
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "simulation/LiveSim.h"

using namespace current_lab;
using render::LabelBox;

TEST(LabelLayout, DeclutterRemovesAllOverlaps) {
    // 12 надписей, наваленных почти в одну точку.
    std::vector<LabelBox> boxes;
    for (int i = 0; i < 12; ++i)
        boxes.push_back({100.0f + (i % 2), 200.0f + 0.5f * i, 90.0f, 18.0f, i});
    ASSERT_TRUE(render::anyOverlap(boxes)) << "тест должен начинаться с перекрытий";

    std::vector<LabelBox> orig = boxes;
    render::declutterVertical(boxes);
    EXPECT_FALSE(render::anyOverlap(boxes)) << "после разведения перекрытия остались";

    // Сдвиг только по вертикали: x сохраняется (сопоставляем по id).
    for (const auto& b : boxes)
        for (const auto& o : orig)
            if (b.id == o.id) EXPECT_FLOAT_EQ(b.x, o.x);
}

TEST(LabelLayout, NoOverlapsOnEveryDemo) {
    const float fontSize = 20.0f; // репрезентативный размер надписи
    int demosWithInitialOverlap = 0;

    for (int d = 0; d < static_cast<int>(demos::DemoCircuit::Count); ++d) {
        auto demo = static_cast<demos::DemoCircuit>(d);
        Circuit circuit = demos::buildDemo(demo);

        CircuitSolver solver;
        simulation::LiveSimConfig simCfg; simCfg.storySeconds = 3.0;
        simulation::LiveSim sim(simCfg);
        sim.onCircuitEvent(circuit, solver);
        CircuitSolution solution = sim.currentSolution(circuit, solver);

        // Самый плотный по надписям режим: показания + мощность + debug (метки узлов).
        projection::ViewParams vp;
        vp.layers.current = true;
        vp.layers.potential = true;
        vp.layers.power = true;
        vp.layers.canvasReadouts = true;
        vp.debugView = true;
        vp.wireThickness = 8.0;

        auto result = projection::buildProjection(projection::ProjectionKind::Physics,
                                                  circuit, &solution, vp);

        CanvasCamera cam = render::computeCameraForCircuit(circuit, 1600, 1000);

        std::vector<LabelBox> boxes;
        boxes.reserve(result.prims.labels.size());
        for (size_t i = 0; i < result.prims.labels.size(); ++i) {
            ImVec2 ws = cam.worldToScreen(result.prims.labels[i].pos);
            boxes.push_back({ws.x, ws.y,
                             render::estimateLabelWidth(result.prims.labels[i].text, fontSize),
                             render::labelLineHeight(fontSize), static_cast<int>(i)});
        }
        if (render::anyOverlap(boxes)) ++demosWithInitialOverlap;

        render::declutterVertical(boxes);
        EXPECT_FALSE(render::anyOverlap(boxes))
            << "перекрытия надписей в демо #" << d << " («" << demos::demoName(demo) << "»)";
    }

    // Тест не вакуумный: хотя бы где-то надписи изначально налезали.
    EXPECT_GT(demosWithInitialOverlap, 0)
        << "ни одна демо не дала исходных перекрытий — тест ничего не проверяет";
}
