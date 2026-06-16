// Тесты ядра «мотор ↔ генератор» (MotorGenerator). Проверяем физику, не рендер.
//
// Конвенция (см. шапку MotorGenerator.h): θ — угол поворота рамки, θ=0 — рамка в
// плоскости поля. Поток Φ=N·B·A·sin θ ⇒
//   ГЕНЕРАТОР: ЭДС ε = N·B·A·ω·cos θ  (макс при θ=0, ноль при θ=±90°, ∝ ω);
//   МОТОР:     момент τ = N·I·A·B·sin θ (макс при θ=90°, ноль при θ=0, ∝ I, ∝ B).
// ЭДС (cos) и момент (sin) в квадратуре — это и проверяем.
#include <gtest/gtest.h>
#include "physics/MotorGenerator.h"
#include <cmath>

using current_lab::physics::MotorGenerator;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Типовое устройство для демки: N витков, площадь A, поле B.
MotorGenerator makeDevice() {
    MotorGenerator d;
    d.setCoil(/*N*/200, /*A*/0.01);   // A = 0.01 м²
    d.setField(/*B*/0.5);             // 0.5 Тл
    return d;
}

} // namespace

// ── Генератор ─────────────────────────────────────────────────────────────────

TEST(MotorGenerator, GeneratorAtRestGivesNoEmf) {
    auto d = makeDevice();
    // ω=0 → ЭДС=0 при любом угле (рамка стоит — ЭДС не наводится).
    for (double th = -kPi; th <= kPi; th += 0.1) {
        EXPECT_DOUBLE_EQ(d.emf(th, 0.0), 0.0) << "θ=" << th;
    }
}

TEST(MotorGenerator, EmfLinearInOmega) {
    auto d = makeDevice();
    const double th = 0.0;                 // максимум ЭДС
    const double base = d.emf(th, 1.0);
    ASSERT_NE(base, 0.0);
    // ЭДС ∝ ω: удвоение/утроение ω масштабирует ЭДС точно так же.
    EXPECT_NEAR(d.emf(th, 2.0), 2.0 * base, 1e-12 * std::abs(base) + 1e-18);
    EXPECT_NEAR(d.emf(th, 5.0), 5.0 * base, 1e-12 * std::abs(base) + 1e-18);
    EXPECT_GT(std::abs(d.emf(th, 10.0)), std::abs(d.emf(th, 1.0)));
}

TEST(MotorGenerator, EmfSinusoidalInAngle) {
    auto d = makeDevice();
    const double w = 3.0;
    const double peak = 200 * 0.5 * 0.01 * w; // N·B·A·ω = εmax
    // Максимум при θ=0, ноль при θ=±90°, минимум (−peak) при θ=π.
    EXPECT_NEAR(d.emf(0.0,        w),  peak, 1e-12 * peak);
    EXPECT_NEAR(d.emf( kPi / 2.0, w),  0.0,  1e-12 * peak + 1e-15);
    EXPECT_NEAR(d.emf(-kPi / 2.0, w),  0.0,  1e-12 * peak + 1e-15);
    EXPECT_NEAR(d.emf(kPi,        w), -peak, 1e-12 * peak);

    // Период 2π: emf(θ) == emf(θ+2π).
    for (double th = -kPi; th <= kPi; th += 0.3) {
        EXPECT_NEAR(d.emf(th, w), d.emf(th + 2.0 * kPi, w), 1e-12 * peak + 1e-15)
            << "θ=" << th;
    }
    // Знак ЭДС меняется при переходе через θ=±90° (cos меняет знак).
    EXPECT_GT(d.emf(0.3, w), 0.0);
    EXPECT_LT(d.emf(kPi - 0.3, w), 0.0);
}

TEST(MotorGenerator, EmfRmsIsPeakOverSqrt2) {
    auto d = makeDevice();
    const double w = 4.0;
    const double peak = d.emf(0.0, w);            // εmax = N·B·A·ω
    ASSERT_GT(peak, 0.0);
    EXPECT_NEAR(d.emfRms(w), peak / std::sqrt(2.0), 1e-12 * peak);
    // RMS не зависит от направления вращения.
    EXPECT_NEAR(d.emfRms(-w), d.emfRms(w), 1e-12 * peak);
    // RMS ∝ ω.
    EXPECT_NEAR(d.emfRms(2.0 * w), 2.0 * d.emfRms(w), 1e-12 * peak);
}

// ── Мотор ─────────────────────────────────────────────────────────────────────

TEST(MotorGenerator, TorqueZeroWhenNoCurrent) {
    auto d = makeDevice();
    // I=0 → момент=0 при любом угле.
    for (double th = -kPi; th <= kPi; th += 0.1) {
        EXPECT_DOUBLE_EQ(d.torque(0.0, th), 0.0) << "θ=" << th;
    }
}

TEST(MotorGenerator, TorqueLinearInCurrentAndField) {
    auto d = makeDevice();
    const double th = kPi / 2.0;            // максимум момента
    const double base = d.torque(1.0, th);
    ASSERT_NE(base, 0.0);
    // τ ∝ I.
    EXPECT_NEAR(d.torque(2.0, th), 2.0 * base, 1e-12 * std::abs(base));
    EXPECT_NEAR(d.torque(3.0, th), 3.0 * base, 1e-12 * std::abs(base));

    // τ ∝ B: удваиваем поле — удваивается момент.
    MotorGenerator d2; d2.setCoil(200, 0.01); d2.setField(1.0); // B вдвое больше
    EXPECT_NEAR(d2.torque(1.0, th), 2.0 * base, 1e-12 * std::abs(base));
}

TEST(MotorGenerator, TorqueSinusoidalInAngle) {
    auto d = makeDevice();
    const double I = 2.0;
    const double peak = 200 * I * 0.01 * 0.5; // N·I·A·B = τmax
    // Момент максимален при θ=90°, ноль при θ=0 и θ=π.
    EXPECT_NEAR(d.torque(I, kPi / 2.0),  peak, 1e-12 * peak);
    EXPECT_NEAR(d.torque(I, 0.0),        0.0,  1e-12 * peak + 1e-15);
    EXPECT_NEAR(d.torque(I, kPi),        0.0,  1e-12 * peak + 1e-15);
    EXPECT_NEAR(d.torque(I, -kPi / 2.0), -peak, 1e-12 * peak);

    // Знак момента меняется с направлением тока.
    EXPECT_NEAR(d.torque(-I, kPi / 2.0), -peak, 1e-12 * peak);
    EXPECT_LT(d.torque(I, kPi / 2.0) * d.torque(-I, kPi / 2.0), 0.0);
}

// ── Квадратура ЭДС и момента ──────────────────────────────────────────────────

TEST(MotorGenerator, EmfAndTorqueAreInQuadrature) {
    auto d = makeDevice();
    const double w = 1.0, I = 1.0;
    // Там, где ЭДС максимальна (θ=0), момент должен быть ~ноль, и наоборот.
    EXPECT_GT(std::abs(d.emf(0.0, w)), 1e-6);
    EXPECT_NEAR(d.torque(I, 0.0), 0.0, 1e-15);
    EXPECT_GT(std::abs(d.torque(I, kPi / 2.0)), 1e-6);
    EXPECT_NEAR(d.emf(kPi / 2.0, w), 0.0, 1e-15);
}

// ── Интегратор ────────────────────────────────────────────────────────────────

TEST(MotorGenerator, MotorSpinsUpFromRest) {
    MotorGenerator d; d.setCoil(200, 0.01); d.setField(0.5);
    d.setInertia(0.001);                   // J = 1e-3 кг·м²
    d.reset();
    // В θ=0 момент равен нулю («мёртвая точка»), поэтому стартуем рамку в θ=π/2,
    // где момент максимален. Угол выставляем публичным stepGenerator, затем
    // обнуляем ω вторым вызовом с ω=0 (угол сохраняется).
    d.stepGenerator(1000.0, (kPi / 2.0) / 1000.0); // θ≈π/2
    d.stepGenerator(0.0, 0.0);                     // ω←0, угол сохранён
    ASSERT_DOUBLE_EQ(d.omega(), 0.0);

    const double dt = 1e-4;
    const double I  = 5.0;
    // Под постоянным током одного знака |ω| монотонно нарастает от нуля.
    d.stepMotor(I, dt);
    const double w1 = d.omega();
    d.stepMotor(I, dt);
    const double w2 = d.omega();
    EXPECT_GT(std::abs(w1), 0.0) << "рамка тронулась из покоя";
    EXPECT_GT(std::abs(w2), std::abs(w1)) << "продолжает разгоняться";
}

// Чистый, без костылей, тест разгона: стартуем в точке максимального момента,
// инициализируя угол через публичный stepGenerator, и проверяем α=τ/J.
TEST(MotorGenerator, MotorAngularAccelerationMatchesTorqueOverInertia) {
    MotorGenerator d; d.setCoil(200, 0.01); d.setField(0.5);
    const double J = 0.002;
    d.setInertia(J);
    d.reset();
    // Установим θ=π/2 (момент максимален) одним генераторным шагом и СБРОСИМ ω,
    // повторно крутанув на 0 за 0 — но это сотрёт... используем: stepGenerator(ω,dt)
    // ставит ω=ω. Чтобы ω=0 при θ=π/2, сделаем stepGenerator(any, dt) затем
    // stepGenerator(0,0): второй вызов оставит угол, обнулит ω.
    d.stepGenerator(1000.0, (kPi / 2.0) / 1000.0); // θ≈π/2
    d.stepGenerator(0.0, 0.0);                     // ω←0, угол сохранён (≈π/2)
    ASSERT_NEAR(d.angle(), kPi / 2.0, 1e-6);
    ASSERT_DOUBLE_EQ(d.omega(), 0.0);

    const double I  = 3.0;
    const double dt = 1e-5;                         // малый шаг → α≈const
    const double tauStart = d.torque(I, d.angle()); // N·I·A·B·sin(π/2)
    const double alphaExpected = tauStart / J;
    d.stepMotor(I, dt);
    // После одного симплектического шага: ω ≈ α·dt.
    EXPECT_NEAR(d.omega(), alphaExpected * dt, 1e-3 * std::abs(alphaExpected * dt) + 1e-12);
    EXPECT_GT(d.omega(), 0.0);                      // разогналась из нуля
}

TEST(MotorGenerator, GeneratorAdvancesAngleMonotonically) {
    auto d = makeDevice();
    d.reset();
    const double w = 5.0;       // ω>0
    const double dt = 1e-3;
    // Угол растёт монотонно (до wrap). Проверим на отрезке без перехода через π.
    double prev = d.angle();
    for (int i = 0; i < 100; ++i) {       // 100·5·1e-3 = 0.5 рад < π
        d.stepGenerator(w, dt);
        EXPECT_GT(d.angle(), prev) << "шаг " << i;
        prev = d.angle();
    }
    EXPECT_NEAR(d.omega(), w, 1e-15);
    // ЭДС обновляется и согласована с формулой.
    EXPECT_NEAR(d.lastEmf(), d.emf(d.angle(), w), 1e-12 * std::abs(d.lastEmf()) + 1e-15);
}

// ── Обратимость (качественно) ─────────────────────────────────────────────────

TEST(MotorGenerator, ReversibilitySameDeviceBothWays) {
    auto d = makeDevice();
    // Одна и та же рамка: крутим (ω≠0) — наводится ЭДС ≠ 0.
    const double w = 6.0;
    EXPECT_GT(std::abs(d.emf(0.0, w)), 1e-6) << "генератор должен давать ЭДС при ω";
    // Той же рамке подаём ток — возникает ненулевой момент (мотор).
    const double I = 4.0;
    EXPECT_GT(std::abs(d.torque(I, kPi / 2.0)), 1e-6) << "мотор должен давать момент при I";
    // Обе стороны масштабируются одними и теми же N, A, B (один прибор):
    // εmax = N·B·A·ω,  τmax = N·I·A·B  — общий множитель N·A·B.
    const double NAB = 200 * 0.01 * 0.5;
    EXPECT_NEAR(d.emf(0.0, w),         NAB * w, 1e-12 * NAB * w);
    EXPECT_NEAR(d.torque(I, kPi/2.0),  NAB * I, 1e-12 * NAB * I);
}
