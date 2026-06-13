#include <gtest/gtest.h>
#include <cmath>
#include "circuit/DemoCircuits.h"
#include "solver/CircuitSolver.h"
#include "ui/I18n.h"

using namespace current_lab::demos;

TEST(DemoCircuits, EveryDemoIsSolvableAndFinite) {
    CircuitSolver solver;
    for (int d = 0; d < static_cast<int>(DemoCircuit::Count); ++d) {
        Circuit c = buildDemo(static_cast<DemoCircuit>(d));
        ASSERT_GE(c.components.size(), 3u) << demoName(static_cast<DemoCircuit>(d));
        EXPECT_GE(c.groundNodeId, 0);
        auto solution = solver.solve(c);
        for (const auto& np : solution.nodePotentials)
            EXPECT_TRUE(std::isfinite(np.potential)) << demoName(static_cast<DemoCircuit>(d));
    }
}

TEST(DemoCircuits, EachFunctionalElementHasItsMinimalDemo) {
    // user 2026-06-13: «минимальный контур на каждый функциональный элемент
    // палитры». Source/Resistor/Capacitor/Inductor/Diode/Switch — у каждого
    // своё минимальное демо; Wire/Ground структурные (есть в каждом контуре).
    auto hasType = [](DemoCircuit d, ComponentType t) {
        Circuit c = buildDemo(d);
        for (const auto& comp : c.components)
            if (comp.type == t) return true;
        return false;
    };
    EXPECT_TRUE(hasType(DemoCircuit::SourceResistor, ComponentType::VoltageSource));
    EXPECT_TRUE(hasType(DemoCircuit::ResistorDivider, ComponentType::Resistor));
    EXPECT_TRUE(hasType(DemoCircuit::RcCapacitor, ComponentType::Capacitor));
    EXPECT_TRUE(hasType(DemoCircuit::RlInductor, ComponentType::Inductor));
    EXPECT_TRUE(hasType(DemoCircuit::DiodeResistor, ComponentType::Diode));
    EXPECT_TRUE(hasType(DemoCircuit::SwitchResistor, ComponentType::Switch));

    // Минимальность: per-element демо не тащат лишние активные/реактивные
    // элементы (это уже комбо SwitchedRc / RLC / пик-детектор).
    EXPECT_FALSE(hasType(DemoCircuit::SwitchResistor, ComponentType::Capacitor));
    EXPECT_FALSE(hasType(DemoCircuit::SwitchResistor, ComponentType::Inductor));
    EXPECT_FALSE(hasType(DemoCircuit::SourceResistor, ComponentType::Capacitor));
    EXPECT_FALSE(hasType(DemoCircuit::SourceResistor, ComponentType::Diode));
    EXPECT_FALSE(hasType(DemoCircuit::ResistorDivider, ComponentType::Inductor));
}

TEST(DemoCircuits, CombosCombineSeveralElementTypes) {
    Circuit rlc = buildDemo(DemoCircuit::RlcSeries);
    bool hasR = false, hasL = false, hasC = false;
    for (const auto& comp : rlc.components) {
        hasR = hasR || comp.type == ComponentType::Resistor;
        hasL = hasL || comp.type == ComponentType::Inductor;
        hasC = hasC || comp.type == ComponentType::Capacitor;
    }
    EXPECT_TRUE(hasR && hasL && hasC);

    Circuit peak = buildDemo(DemoCircuit::PeakDetector);
    bool hasD = false, hasC2 = false;
    for (const auto& comp : peak.components) {
        hasD = hasD || comp.type == ComponentType::Diode;
        hasC2 = hasC2 || comp.type == ComponentType::Capacitor;
    }
    EXPECT_TRUE(hasD && hasC2);

    // SwitchedRc — комбо ключ + RC (переходный по щелчку), отдельно от
    // минимального демо ключа (SwitchResistor).
    Circuit swrc = buildDemo(DemoCircuit::SwitchedRc);
    bool hasSw = false, hasC3 = false;
    for (const auto& comp : swrc.components) {
        hasSw = hasSw || comp.type == ComponentType::Switch;
        hasC3 = hasC3 || comp.type == ComponentType::Capacitor;
    }
    EXPECT_TRUE(hasSw && hasC3);

    // Peak detector actually holds the peak in transient.
    CircuitSolver solver;
    TransientState state;
    Circuit c = buildDemo(DemoCircuit::PeakDetector);
    for (int i = 0; i < 500; ++i)
        solver.stepTransient(c, state, 1e-3);
    double held = 0.0;
    for (const auto& [id, vc] : state.capVoltage) held = std::max(held, vc);
    EXPECT_GT(held, 4.5);
}

TEST(DemoCircuits, EveryDemoNameIsTranslated) {
    current_lab::i18n::setLanguage(current_lab::i18n::Language::Russian);
    for (int d = 0; d < static_cast<int>(DemoCircuit::Count); ++d) {
        const char* en = demoName(static_cast<DemoCircuit>(d));
        EXPECT_STRNE(current_lab::i18n::tr(en), en) << en; // has RU translation
    }
    current_lab::i18n::setLanguage(current_lab::i18n::Language::English);
}

// Font-atlas coverage: every translated string is part of the atlas text, so
// typographic characters can never render as "?".
TEST(I18nAtlas, AllUiTextCoversTypography) {
    std::string text = current_lab::i18n::allUiText();
    EXPECT_NE(text.find("\xE2\x80\x94"), std::string::npos); // — em dash
    EXPECT_NE(text.find("\xC2\xAB"), std::string::npos);     // «
    EXPECT_NE(text.find("\xC2\xB5"), std::string::npos);     // µ
    EXPECT_NE(text.find("\xCE\xA9"), std::string::npos);     // Ω
    EXPECT_NE(text.find("Демо"), std::string::npos);
}
