#include <gtest/gtest.h>
#include <cstring>
#include "learning/Lessons.h"
#include "solver/CircuitSolver.h"
#include "ui/DualViewState.h"
#include "ui/I18n.h"
#include "visualization/VisualizationPresets.h"

// --- fix: animation works out of the box, presets stay meaningful ------------
// The startup preset is Current / Drift (animated layers on); every preset
// produces a distinct layer set, so switching presets is a visible change.

TEST(Presets, DefaultPresetHasAnimatedLayers) {
    auto info = current_lab::visualization::presetInfo(
        current_lab::visualization::kDefaultPreset);
    EXPECT_TRUE(info.layers.current);
    EXPECT_TRUE(info.layers.drift);
}

TEST(Presets, SwitchingPresetsChangesLayers) {
    using current_lab::visualization::VisualizationPreset;
    using current_lab::visualization::presetInfo;
    auto a = presetInfo(VisualizationPreset::Potential).layers;
    auto b = presetInfo(VisualizationPreset::CurrentDrift).layers;
    auto c = presetInfo(VisualizationPreset::PowerHeat).layers;
    EXPECT_NE(a.drift, b.drift);
    EXPECT_NE(a.potential, b.potential);
    EXPECT_NE(b.heat, c.heat);
}

// --- fix: resizable split panes ------------------------------------------------

TEST(PaneSplitters, DualRatioMovesTheBoundary) {
    auto left = current_lab::ui::computeDualViewPaneSplit(1000.0f, 8.0f, 0.3f);
    auto even = current_lab::ui::computeDualViewPaneSplit(1000.0f, 8.0f, 0.5f);
    auto right = current_lab::ui::computeDualViewPaneSplit(1000.0f, 8.0f, 0.7f);
    EXPECT_LT(left.circuitWidth, even.circuitWidth);
    EXPECT_LT(even.circuitWidth, right.circuitWidth);
    EXPECT_NEAR(right.circuitWidth + right.physicsWidth + 8.0f, 1000.0f, 2.0f);
}

TEST(PaneSplitters, RatioIsClampedToKeepPanesUsable) {
    EXPECT_FLOAT_EQ(current_lab::ui::clampPaneRatio(0.01f), current_lab::ui::kMinPaneRatio);
    EXPECT_FLOAT_EQ(current_lab::ui::clampPaneRatio(0.99f), 1.0f - current_lab::ui::kMinPaneRatio);
    auto extreme = current_lab::ui::computeDualViewPaneSplit(1000.0f, 8.0f, 0.001f);
    EXPECT_GT(extreme.circuitWidth, 100.0f); // never collapses to zero
}

TEST(PaneSplitters, TripleRatiosKeepEveryPaneAlive) {
    auto split = current_lab::ui::computeTripleViewPaneSplit(1500.0f, 8.0f, 0.6f, 0.5f);
    // ratio2 gets re-clamped so the third pane keeps its minimum share.
    EXPECT_GT(split.mechanicsWidth, 100.0f);
    EXPECT_GT(split.circuitWidth, split.physicsWidth);
    EXPECT_NEAR(split.circuitWidth + split.physicsWidth + split.mechanicsWidth + 16.0f,
                1500.0f, 3.0f);
}

TEST(PaneSplitters, DefaultArgumentsKeepOldBehaviour) {
    auto dual = current_lab::ui::computeDualViewPaneSplit(700.0f, 8.0f);
    EXPECT_NEAR(dual.circuitWidth, (700.0f - 8.0f) * 0.5f, 1.0f);
    auto triple = current_lab::ui::computeTripleViewPaneSplit(1200.0f, 8.0f);
    EXPECT_NEAR(triple.circuitWidth, triple.physicsWidth, 1.0f);
}

// --- fix: RU/EN language switch -------------------------------------------------

TEST(I18n, EnglishIsDefaultAndIdentity) {
    current_lab::i18n::setLanguage(current_lab::i18n::Language::English);
    EXPECT_STREQ(current_lab::i18n::tr("Reset Demo"), "Reset Demo");
}

TEST(I18n, RussianTranslatesKnownStrings) {
    current_lab::i18n::setLanguage(current_lab::i18n::Language::Russian);
    EXPECT_STREQ(current_lab::i18n::tr("Reset Demo"), "Демо заново");
    EXPECT_STREQ(current_lab::i18n::tr("Capacitor"), "Конденсатор");
    current_lab::i18n::setLanguage(current_lab::i18n::Language::English);
}

TEST(I18n, UnknownStringsFallBackToEnglish) {
    current_lab::i18n::setLanguage(current_lab::i18n::Language::Russian);
    EXPECT_STREQ(current_lab::i18n::tr("some untranslated text"), "some untranslated text");
    current_lab::i18n::setLanguage(current_lab::i18n::Language::English);
}

TEST(I18n, FormatStringsKeepPrintfSignature) {
    current_lab::i18n::setLanguage(current_lab::i18n::Language::Russian);
    const char* pairs[] = {"t = %.3f s", "Mode: %s", "Preset: %s", "Your answer (%s)"};
    for (const char* key : pairs) {
        const char* ru = current_lab::i18n::tr(key);
        int countEn = 0, countRu = 0;
        for (const char* c = key; *c; ++c) countEn += (*c == '%');
        for (const char* c = ru; *c; ++c) countRu += (*c == '%');
        EXPECT_EQ(countEn, countRu) << key;
    }
    current_lab::i18n::setLanguage(current_lab::i18n::Language::English);
}

// --- feature: lesson preset circuits ---------------------------------------------

TEST(LessonPresets, EveryFamilyProducesASolvableCircuit) {
    using namespace current_lab::learning;
    CircuitSolver solver;
    for (int f = 0; f < static_cast<int>(TaskFamily::Count); ++f) {
        Circuit circuit = lessonPresetCircuit(static_cast<TaskFamily>(f));
        ASSERT_GE(circuit.components.size(), 3u);
        EXPECT_GE(circuit.groundNodeId, 0);

        bool hasSource = false;
        for (const auto& comp : circuit.components)
            hasSource = hasSource || comp.type == ComponentType::VoltageSource;
        EXPECT_TRUE(hasSource);

        auto solution = solver.solve(circuit);
        EXPECT_EQ(solution.nodePotentials.size(), circuit.nodes.size());
        for (const auto& np : solution.nodePotentials)
            EXPECT_TRUE(std::isfinite(np.potential));
    }
}

TEST(LessonPresets, FamiliesContainTheirSignatureElement) {
    using namespace current_lab::learning;
    auto hasType = [](const Circuit& c, ComponentType t) {
        for (const auto& comp : c.components)
            if (comp.type == t) return true;
        return false;
    };
    EXPECT_TRUE(hasType(lessonPresetCircuit(TaskFamily::RcTimeConstant), ComponentType::Capacitor));
    EXPECT_TRUE(hasType(lessonPresetCircuit(TaskFamily::RlTimeConstant), ComponentType::Inductor));
    EXPECT_TRUE(hasType(lessonPresetCircuit(TaskFamily::OhmsLaw), ComponentType::Resistor));

    // Parallel preset: two resistors share the same node pair.
    Circuit parallel = lessonPresetCircuit(TaskFamily::ParallelResistors);
    int resistorCount = 0;
    for (const auto& comp : parallel.components)
        if (comp.type == ComponentType::Resistor) ++resistorCount;
    EXPECT_EQ(resistorCount, 2);
}

// --- fix: voltage source +/- glyphs must not overlap ---------------------------

#include "circuit/Circuit.h"
#include "projection/ProjectionBuilder.h"

TEST(VoltageSourceSymbol, PlusAndMinusGlyphsAreSeparated) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, n2, 5.0);

    CircuitSolver solver;
    auto solution = solver.solve(c);
    auto result = current_lab::projection::buildProjection(
        current_lab::projection::ProjectionKind::Schematic, c, &solution,
        current_lab::projection::ViewParams{});

    const uint32_t red = current_lab::render::packColor(255, 100, 100);
    const uint32_t blue = current_lab::render::packColor(100, 100, 255);

    std::vector<Vec2> plusMids, minusMids;
    for (const auto& line : result.prims.lines) {
        Vec2 mid = (line.a + line.b) * 0.5;
        if (line.color == red) plusMids.push_back(mid);
        if (line.color == blue) minusMids.push_back(mid);
    }
    ASSERT_EQ(plusMids.size(), 2u);  // the two strokes of "+"
    ASSERT_EQ(minusMids.size(), 1u); // the single stroke of "-"

    // Both "+" strokes cross at one point...
    EXPECT_NEAR((plusMids[0] - plusMids[1]).length(), 0.0, 1e-6);
    // ...which is clearly separated from the "-" glyph.
    EXPECT_GT((plusMids[0] - minusMids[0]).length(), 10.0);
}
