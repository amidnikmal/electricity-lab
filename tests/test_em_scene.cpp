// Тесты демо-сцен ЭМ-поля (физика, не рендер):
//  - в диэлектрике ε_r=4 фронт идёт со скоростью ≈ c/2 (Максвелл в среде);
//  - PEC-волновод удерживает поле в канале (снаружи пластин ≈ 0);
//  - экран с двумя щелями блокирует волну (за сплошной частью ≈ 0, за щелью > 0).
#include <gtest/gtest.h>
#include "physics/FdtdField.h"
#include "physics/EmScene.h"
#include <cmath>
#include <vector>

using namespace current_lab::physics;

namespace {
double pulse(int n, double n0, double spread) {
    double x = (n - n0) / spread;
    return -x * std::exp(-x * x);
}
} // namespace

TEST(EmScene, DielectricHalvesWaveSpeed) {
    FdtdConfig cfg; cfg.nx = cfg.ny = cfg.nz = 60; cfg.cellSize = 1e-3;
    FdtdField sim(cfg);
    for (int i = 0; i < 60; ++i)               // вся область — ε_r = 4
        for (int j = 0; j < 60; ++j)
            for (int k = 0; k < 60; ++k) sim.setEpsR(i, j, k, 4.0);
    sim.finalizeMaterials();

    const int c0 = 30, obs1 = 40, obs2 = 48;
    const double sep = (obs2 - obs1) * cfg.cellSize;
    const int steps = 260;
    std::vector<double> s1(steps), s2(steps);
    for (int n = 0; n < steps; ++n) {
        sim.addSoftSource(c0, c0, c0, 2, pulse(n, 20.0, 6.0));
        sim.step();
        s1[n] = std::fabs(sim.ez(obs1, c0, c0));
        s2[n] = std::fabs(sim.ez(obs2, c0, c0));
    }
    auto arrival = [&](const std::vector<double>& s) {
        double peak = 0; for (double v : s) peak = std::max(peak, v);
        for (int n = 0; n < (int)s.size(); ++n) if (s[n] >= 0.25 * peak) return n;
        return -1;
    };
    int a1 = arrival(s1), a2 = arrival(s2);
    ASSERT_GT(a1, 0); ASSERT_GT(a2, a1);
    double measured = sep / ((a2 - a1) * sim.dt());
    double expected = FdtdField::lightSpeed() / 2.0; // v = c/√ε_r
    EXPECT_GT(measured, 0.80 * expected) << "measured=" << measured << " exp=" << expected;
    EXPECT_LT(measured, 1.20 * expected) << "measured=" << measured << " exp=" << expected;
}

TEST(EmScene, WaveguideConfinesField) {
    FdtdConfig cfg; cfg.nx = cfg.ny = cfg.nz = 56;
    FdtdField sim(cfg);
    EmSource src = buildEmScene(sim, EmDemo::Waveguide);

    const int mid = 56 / 2, kc = 56 / 2;
    const int jhi = mid + 56 / 6;
    double insidePeak = 0.0, outsidePeak = 0.0;
    for (int n = 0; n < 220; ++n) {
        injectEmSource(sim, src, n);
        sim.step();
        insidePeak  = std::max(insidePeak,  sim.eMag(mid, mid,      kc)); // в канале
        outsidePeak = std::max(outsidePeak, sim.eMag(mid, jhi + 4,  kc)); // за пластиной
    }
    ASSERT_GT(insidePeak, 0.0) << "в канале нет поля";
    // PEC-стенка экранирует наружную область.
    EXPECT_LT(outsidePeak, 0.1 * insidePeak)
        << "inside=" << insidePeak << " outside=" << outsidePeak;
}

TEST(EmScene, DoubleSlitScreenBlocks) {
    FdtdConfig cfg; cfg.nx = cfg.ny = cfg.nz = 60;
    FdtdField sim(cfg);
    EmSource src = buildEmScene(sim, EmDemo::DoubleSlit);

    const int screenI = 60 / 2, jc = 60 / 2, kc = 60 / 2;
    const int sep = 60 / 6;
    double behindSlit = 0.0, behindSolid = 0.0;
    for (int n = 0; n < 240; ++n) {
        injectEmSource(sim, src, n);
        sim.step();
        // на одну ячейку за экраном: за щелью поле проходит, за сплошной частью — нет
        behindSlit  = std::max(behindSlit,  sim.eMag(screenI + 1, jc - sep, kc));
        behindSolid = std::max(behindSolid, sim.eMag(screenI + 1, jc,       kc));
    }
    ASSERT_GT(behindSlit, 0.0) << "сквозь щель поле не прошло";
    EXPECT_LT(behindSolid, 0.2 * behindSlit)
        << "slit=" << behindSlit << " solid=" << behindSolid;
}
