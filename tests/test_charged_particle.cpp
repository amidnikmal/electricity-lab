// Тесты силы Лоренца (Boris pusher): энергия сохраняется в B, радиус циклотрона,
// дрейф E×B. Чистая физика, без рендера.
#include <gtest/gtest.h>
#include "physics/ChargedParticle.h"
#include <cmath>

using namespace current_lab::physics;

TEST(ChargedParticle, SpeedConservedInPureMagneticField) {
    double q = 1.0, m = 1.0, Bz = 2.0, v0 = 3.0;
    ParticleState s{ Vec2(0, 0), Vec2(v0, 0) };
    double T = 2.0 * 3.14159265358979 / std::abs(cyclotronAngularFreq(q, m, Bz));
    double dt = T / 400.0;
    for (int i = 0; i < 4000; ++i) s = borisStep(s, q, m, Vec2(), Bz, dt);
    double speed = s.vel.length();
    EXPECT_NEAR(speed, v0, v0 * 1e-3) << "энергия не сохраняется в чистом B";
}

TEST(ChargedParticle, CyclotronRadiusMatchesTheory) {
    double q = 1.0, m = 1.0, Bz = 1.5, v0 = 4.0;
    ParticleState s{ Vec2(0, 0), Vec2(v0, 0) };
    double T = 2.0 * 3.14159265358979 / std::abs(cyclotronAngularFreq(q, m, Bz));
    double dt = T / 600.0;
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (int i = 0; i < 600; ++i) { // ровно один оборот
        s = borisStep(s, q, m, Vec2(), Bz, dt);
        minX = std::min(minX, s.pos.x); maxX = std::max(maxX, s.pos.x);
        minY = std::min(minY, s.pos.y); maxY = std::max(maxY, s.pos.y);
    }
    double rMeasured = 0.25 * ((maxX - minX) + (maxY - minY)); // среднее полудиаметра
    double rTheory = cyclotronRadius(m, v0, q, Bz);
    EXPECT_NEAR(rMeasured, rTheory, rTheory * 0.03)
        << "r=" << rMeasured << " теория=" << rTheory;
}

TEST(ChargedParticle, ExBDriftMatchesEOverB) {
    // Скрещённые поля: E вдоль x, B вдоль z. Дрейф = E×B/B² (вдоль −y при E_x>0).
    double q = 1.0, m = 1.0, Bz = 2.0;
    Vec2 E(1.5, 0.0);
    ParticleState s{ Vec2(0, 0), Vec2(0, 0) };
    double T = 2.0 * 3.14159265358979 / std::abs(cyclotronAngularFreq(q, m, Bz));
    double dt = T / 400.0;
    int steps = 4000;
    for (int i = 0; i < steps; ++i) s = borisStep(s, q, m, E, Bz, dt);
    // Средняя скорость (по смещению) ≈ дрейф E×B (циклотронная часть усредняется в ноль).
    Vec2 meanVel = s.pos * (1.0 / (steps * dt));
    Vec2 drift = exbDrift(E, Bz);
    EXPECT_NEAR(meanVel.x, drift.x, std::abs(drift.y) * 0.05 + 0.02);
    EXPECT_NEAR(meanVel.y, drift.y, std::abs(drift.y) * 0.05 + 0.02)
        << "дрейф=" << meanVel.y << " теория=" << drift.y;
    EXPECT_GT(std::abs(drift.y), 0.1); // тест не вакуумный
}
