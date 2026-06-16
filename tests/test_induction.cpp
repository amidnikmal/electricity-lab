// Тесты ядра индукции Фарадея (InductionModel). Проверяем физику, а не рендер:
//  - неподвижный магнит (v=0) не даёт ЭДС — ключевое заблуждение «магнит рядом = ток»;
//  - |ЭДС| растёт линейно со скоростью;
//  - правило Ленца: знак ЭДС переворачивается при v→−v и при пролёте центра (d=0);
//  - поток монотонно спадает по |d| и пропорционален числу витков N;
//  - аналитическая производная согласуется с численной.
#include <gtest/gtest.h>
#include "physics/InductionModel.h"
#include <cmath>

using current_lab::physics::InductionModel;

namespace {

// Типовая катушка/магнит для демки.
InductionModel makeModel() {
    InductionModel m;
    m.setCoil(/*N*/120, /*R*/0.05);
    m.setMagnet(/*m*/12.0);
    return m;
}

} // namespace

TEST(Induction, RestGivesNoEmf) {
    auto model = makeModel();
    // При v=0 ЭДС ровно ноль при любой позиции магнита — неподвижный магнит тока не даёт.
    for (double d = -0.3; d <= 0.3; d += 0.01) {
        EXPECT_DOUBLE_EQ(model.emf(d, 0.0), 0.0) << "d=" << d;
    }
}

TEST(Induction, EmfLinearInVelocity) {
    auto model = makeModel();
    const double d = 0.03;             // магнит вне центра, ЭДС ненулевая
    const double base = model.emf(d, 1.0);
    ASSERT_NE(base, 0.0);
    // |ЭДС| ∝ v: удвоение/утроение скорости масштабирует ЭДС точно так же.
    EXPECT_NEAR(model.emf(d, 2.0), 2.0 * base, 1e-12 * std::abs(base) + 1e-18);
    EXPECT_NEAR(model.emf(d, 3.0), 3.0 * base, 1e-12 * std::abs(base) + 1e-18);
    // Быстрее → больше по модулю.
    EXPECT_GT(std::abs(model.emf(d, 5.0)), std::abs(model.emf(d, 1.0)));
}

TEST(Induction, LenzSignFlips) {
    auto model = makeModel();
    const double d = 0.04;
    // Смена направления движения переворачивает знак ЭДС.
    EXPECT_NEAR(model.emf(d, +1.0), -model.emf(d, -1.0), 1e-15);
    EXPECT_LT(model.emf(d, +1.0) * model.emf(d, -1.0), 0.0);

    // Пролёт центра: одинаковая скорость, разные стороны → противоположные знаки.
    const double v = 1.0;
    const double left  = model.emf(-d, v); // подлёт
    const double right = model.emf(+d, v); // отлёт
    EXPECT_LT(left * right, 0.0) << "знак ЭДС не меняется при пролёте центра";
    EXPECT_NEAR(left, -right, 1e-15) << "ЭДС должна быть нечётна по d";

    // В самом центре ЭДС обращается в ноль (мгновенный переход знака).
    EXPECT_NEAR(model.emf(0.0, v), 0.0, 1e-18);
}

TEST(Induction, FluxDecaysWithDistanceAndScalesWithTurns) {
    auto model = makeModel();
    // Поток максимален в центре и монотонно убывает по |d| (по обе стороны).
    double prev = model.totalFlux(0.0);
    for (double d = 0.005; d <= 0.3; d += 0.005) {
        double cur = model.totalFlux(d);
        EXPECT_LT(cur, prev) << "поток не убывает при d=" << d;
        // Симметрия по знаку d (поток чётен).
        EXPECT_NEAR(model.totalFlux(d), model.totalFlux(-d), 1e-18);
        prev = cur;
    }

    // Φ ∝ N: удваиваем витки — удваивается полный поток (Φ₁ от N не зависит).
    InductionModel m1; m1.setCoil(100, 0.05); m1.setMagnet(12.0);
    InductionModel m2; m2.setCoil(200, 0.05); m2.setMagnet(12.0);
    const double d = 0.02;
    EXPECT_NEAR(m2.totalFlux(d), 2.0 * m1.totalFlux(d), 1e-15 * std::abs(m1.totalFlux(d)));
    EXPECT_NEAR(m1.totalFlux(d), m1.fluxPerTurn(d) * 100.0, 1e-18);
}

TEST(Induction, AnalyticDerivativeMatchesNumeric) {
    auto model = makeModel();
    const int N = 120;
    const double v = 1.0;
    const double eps = 1e-6;
    // emf(d,v) = −N·dΦ₁/dd·v. Сверяем dΦ₁/dd с центральной разностью.
    for (double d = -0.1; d <= 0.1; d += 0.01) {
        if (std::abs(d) < 1e-9) continue; // в самом центре сравнение тривиально (0≈0)
        double numericEmf =
            -N * (model.fluxPerTurn(d + eps) - model.fluxPerTurn(d - eps)) / (2.0 * eps) * v;
        double analyticEmf = model.emf(d, v);
        EXPECT_NEAR(analyticEmf, numericEmf, 1e-6 * std::abs(analyticEmf) + 1e-9)
            << "d=" << d;
    }
}

TEST(Induction, LampDarkWhenStillBrightWhenMoving) {
    auto model = makeModel();
    const double d = 0.03;
    // Стоит — темно.
    EXPECT_DOUBLE_EQ(model.lampBrightness(d, 0.0), 0.0);
    // Двигается — светит, яркость в [0,1] и растёт со скоростью.
    double bSlow = model.lampBrightness(d, 1.0);
    double bFast = model.lampBrightness(d, 20.0);
    EXPECT_GT(bSlow, 0.0);
    EXPECT_LT(bSlow, 1.0);
    EXPECT_GE(bFast, 0.0);
    EXPECT_LE(bFast, 1.0);
    EXPECT_GT(bFast, bSlow) << "быстрее → ярче";
}
