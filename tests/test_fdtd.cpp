// Тесты ядра 3D-FDTD (уравнения Максвелла). Проверяем физику, а не рендер:
//  - скорость распространения фронта ≈ c (по задержке пика поля);
//  - устойчивость + поглощение (энергия конечна и спадает после ухода импульса);
//  - симметрия поля точечного источника в вакууме.
#include <gtest/gtest.h>
#include "physics/FdtdField.h"
#include <cmath>
#include <vector>

using current_lab::physics::FdtdField;
using current_lab::physics::FdtdConfig;

namespace {

// Биполярный импульс с НУЛЕВЫМ средним (производная гауссиана). Важно для мягкого
// источника: у обычного гауссиана есть DC-составляющая, оставляющая незатухающий
// статический след поля. Зеро-DC импульс полностью излучается и поглощается.
double pulse(int n, double n0, double spread) {
    double x = (n - n0) / spread;
    return -x * std::exp(-x * x);
}

} // namespace

TEST(Fdtd, WaveFrontTravelsAtLightSpeed) {
    FdtdConfig cfg;
    cfg.nx = cfg.ny = cfg.nz = 60;
    cfg.cellSize = 1e-3;
    FdtdField sim(cfg);

    const int c0 = 30;                 // источник в центре
    // Дифференциальное измерение: два приёмника на оси x. Скорость = разность
    // расстояний / разность времён прихода пика — это убирает сдвиг формы
    // источника и эффекты ближнего поля.
    const int obs1 = 40, obs2 = 48;    // оба в вакууме (поглотитель с 52-й ячейки)
    const double sep = (obs2 - obs1) * cfg.cellSize;

    const double n0 = 20.0, spread = 6.0;
    const int steps = 170;
    std::vector<double> s1(steps), s2(steps);

    for (int n = 0; n < steps; ++n) {
        sim.addSoftSource(c0, c0, c0, /*Ez*/2, pulse(n, n0, spread));
        sim.step();
        s1[n] = std::fabs(sim.ez(obs1, c0, c0));
        s2[n] = std::fabs(sim.ez(obs2, c0, c0));
    }

    // Время прихода фронта = первое пересечение 25% от собственного пика приёмника.
    auto arrival = [&](const std::vector<double>& s) {
        double peak = 0.0; for (double v : s) peak = std::max(peak, v);
        for (int n = 0; n < (int)s.size(); ++n) if (s[n] >= 0.25 * peak) return n;
        return -1;
    };
    int a1 = arrival(s1), a2 = arrival(s2);
    ASSERT_GT(a1, 0); ASSERT_GT(a2, a1) << "волна не дошла до дальнего приёмника";
    double delay = (a2 - a1) * sim.dt();
    double measured = sep / delay;
    double c = FdtdField::lightSpeed();

    // FDTD занижает фазовую скорость (числовая дисперсия); пик ловим грубо.
    EXPECT_GT(measured, 0.80 * c) << "measured=" << measured << " c=" << c;
    EXPECT_LT(measured, 1.15 * c) << "measured=" << measured << " c=" << c;
}

TEST(Fdtd, StableAndAbsorbing) {
    FdtdConfig cfg;
    cfg.nx = cfg.ny = cfg.nz = 48;
    FdtdField sim(cfg);

    const int c0 = 24;
    double peakEnergy = 0.0;
    // 40 шагов с импульсом — накачка.
    for (int n = 0; n < 40; ++n) {
        sim.addSoftSource(c0, c0, c0, 2, pulse(n, 18.0, 5.0));
        sim.step();
        peakEnergy = std::max(peakEnergy, sim.totalEnergy());
    }
    // 400 шагов без источника — волна должна уйти в поглощающий слой.
    for (int n = 0; n < 400; ++n) sim.step();

    double endEnergy = sim.totalEnergy();
    ASSERT_TRUE(std::isfinite(endEnergy)) << "энергия не конечна (взрыв схемы)";
    EXPECT_GT(peakEnergy, 0.0);
    // Поглощающая граница: к концу энергии существенно меньше пика.
    EXPECT_LT(endEnergy, 0.2 * peakEnergy)
        << "end=" << endEnergy << " peak=" << peakEnergy;
}

TEST(Fdtd, PointSourceIsSymmetric) {
    FdtdConfig cfg;
    cfg.nx = cfg.ny = cfg.nz = 50;
    FdtdField sim(cfg);

    const int c0 = 25;
    for (int n = 0; n < 60; ++n) {
        sim.addSoftSource(c0, c0, c0, 2, pulse(n, 20.0, 6.0));
        sim.step();
    }
    // Ez(источник на Ez) симметричен при отражении по x и по y относительно центра.
    for (int d = 3; d <= 10; ++d) {
        double px = sim.ez(c0 + d, c0, c0), mx = sim.ez(c0 - d, c0, c0);
        double py = sim.ez(c0, c0 + d, c0), my = sim.ez(c0, c0 - d, c0);
        EXPECT_NEAR(px, mx, 1e-4 + 0.02 * std::fabs(px)) << "ассим. по x при d=" << d;
        EXPECT_NEAR(py, my, 1e-4 + 0.02 * std::fabs(py)) << "ассим. по y при d=" << d;
    }
}
