// Тесты электростатики точечных зарядов (Кулон, суперпозиция). Чистая физика.
#include <gtest/gtest.h>
#include "physics/CoulombFieldModel.h"
#include <cmath>

using namespace current_lab::physics;

TEST(CoulombField, PointChargeFalloffAndDirection) {
    CoulombConfig cfg; cfg.k = 1.0; cfg.softening = 0.0;
    std::vector<PointCharge> q{ { Vec2(0, 0), 2.0 } };
    // Поле направлено наружу от +заряда, спадает ~1/r².
    Vec2 e1 = coulombField(Vec2(1, 0), q, cfg);
    Vec2 e2 = coulombField(Vec2(2, 0), q, cfg);
    EXPECT_GT(e1.x, 0.0);                       // наружу
    EXPECT_NEAR(e1.y, 0.0, 1e-12);
    EXPECT_NEAR(e1.x / e2.x, 4.0, 1e-6);        // (2/1)² = 4
    EXPECT_NEAR(e1.x, 2.0, 1e-9);               // kq/r² = 1*2/1
}

TEST(CoulombField, SuperpositionDipoleMidpointFieldPointsNegative) {
    CoulombConfig cfg; cfg.k = 1.0; cfg.softening = 0.0;
    std::vector<PointCharge> dip{ { Vec2(-1, 0), +1.0 }, { Vec2(1, 0), -1.0 } };
    // В середине диполя поле от обоих складывается и направлено от + к − (вдоль +x).
    Vec2 e = coulombField(Vec2(0, 0), dip, cfg);
    EXPECT_GT(e.x, 0.0);
    EXPECT_NEAR(e.y, 0.0, 1e-12);
    // Потенциал в середине ≈ 0 (равные и противоположные заряды на равном расстоянии).
    EXPECT_NEAR(coulombPotential(Vec2(0, 0), dip, cfg), 0.0, 1e-9);
}

TEST(CoulombField, FieldIsNegativeGradientOfPotential) {
    CoulombConfig cfg; cfg.k = 1.0; cfg.softening = 0.5;
    std::vector<PointCharge> q{ { Vec2(0, 0), 1.0 }, { Vec2(3, 1), -0.7 } };
    Vec2 p(1.2, 0.4);
    double h = 1e-4;
    double dphidx = (coulombPotential(p + Vec2(h, 0), q, cfg) -
                     coulombPotential(p - Vec2(h, 0), q, cfg)) / (2 * h);
    double dphidy = (coulombPotential(p + Vec2(0, h), q, cfg) -
                     coulombPotential(p - Vec2(0, h), q, cfg)) / (2 * h);
    Vec2 e = coulombField(p, q, cfg);
    EXPECT_NEAR(e.x, -dphidx, 1e-3) << "E ≠ −∂φ/∂x";
    EXPECT_NEAR(e.y, -dphidy, 1e-3) << "E ≠ −∂φ/∂y";
}
