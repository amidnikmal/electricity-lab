// Тесты 1D поперечной волны на струне (StringWave). Проверяем физику аналогии:
//  - покой: без драйва верёвка плоская;
//  - бегущая волна: фронт распространяется с конечной скоростью (близкая точка
//    задета раньше дальней);
//  - стоячая волна: закреплённый конец → узлы и пучности (значимый разброс
//    амплитуд по длине), в отличие от поглощающего конца (бегущая, ровнее);
//  - поглощение: уходящая волна почти не отражается (нет стоячей картины).
#include <gtest/gtest.h>
#include "physics/StringWave.h"
#include <vector>
#include <cmath>

using current_lab::physics::StringWave;

namespace {

// Амплитуда колебаний каждой точки за окно времени: 0.5·(max - min) по слоям.
// Возвращает вектор размера n().
std::vector<double> perPointAmplitude(StringWave& w, int periods, int stepsPerPeriod) {
    const int n = w.n();
    std::vector<double> ymin(n, +1e300), ymax(n, -1e300);
    for (int p = 0; p < periods * stepsPerPeriod; ++p) {
        w.advance(1);
        for (int i = 0; i < n; ++i) {
            double v = w.y(i);
            ymin[i] = std::min(ymin[i], v);
            ymax[i] = std::max(ymax[i], v);
        }
    }
    std::vector<double> amp(n);
    for (int i = 0; i < n; ++i) amp[i] = 0.5 * (ymax[i] - ymin[i]);
    return amp;
}

} // namespace

TEST(StringWave, RestStaysFlat) {
    StringWave w(50);
    // Драйв не задан (amp = 0) — верёвка должна остаться плоской.
    w.advance(2000);
    EXPECT_LT(w.maxAbs(), 1e-9);
}

TEST(StringWave, FrontTravelsAtFiniteSpeed) {
    StringWave w(120);
    w.setFarEnd(StringWave::FarEnd::Absorbing); // чтобы не было отражений на этом тесте
    w.setDrive(/*freq*/ 0.01, /*amp*/ 1.0);

    const int near = 10;   // ближняя точка
    const int far  = 90;   // дальняя точка
    const double eps = 1e-4;

    int tNear = -1, tFar = -1;
    for (int step = 0; step < 4000; ++step) {
        w.advance(1);
        if (tNear < 0 && std::fabs(w.y(near)) > eps) tNear = step;
        if (tFar  < 0 && std::fabs(w.y(far))  > eps) tFar  = step;
        if (tNear >= 0 && tFar >= 0) break;
    }
    ASSERT_GE(tNear, 0) << "возмущение не дошло до ближней точки";
    ASSERT_GE(tFar, 0)  << "возмущение не дошло до дальней точки";
    // Конечная скорость: дальняя точка возбуждается строго позже ближней.
    EXPECT_GT(tFar, tNear)
        << "tNear=" << tNear << " tFar=" << tFar;
}

TEST(StringWave, FixedEndMakesStandingWave) {
    // Закреплённый дальний конец → стоячая волна: должны быть узлы (~0) и пучности.
    StringWave w(80);
    w.setFarEnd(StringWave::FarEnd::Fixed);
    // Частота подобрана так, чтобы на длине укладывалось несколько полуволн.
    w.setDrive(/*freq*/ 0.02, /*amp*/ 1.0);

    const int stepsPerPeriod = static_cast<int>(1.0 / 0.02); // = 50
    // Прогрев: дать установиться стоячей картине.
    w.advance(60 * stepsPerPeriod);
    // Измеряем амплитуды по длине за несколько периодов.
    auto amp = perPointAmplitude(w, /*periods*/ 8, stepsPerPeriod);

    // Ищем максимум амплитуды по внутренним точкам (пучность) и минимум (узел).
    double aMax = 0.0, aMin = 1e300;
    for (int i = 1; i < w.n() - 1; ++i) {
        aMax = std::max(aMax, amp[i]);
        aMin = std::min(aMin, amp[i]);
    }
    ASSERT_GT(aMax, 1e-6) << "верёвка не раскачалась";
    // Есть узлы: где-то амплитуда сильно меньше пучности.
    EXPECT_LT(aMin, 0.3 * aMax)
        << "нет выраженных узлов: aMin=" << aMin << " aMax=" << aMax;

    // Сравнение с бегущей (поглощающей): у неё разброс амплитуд заметно меньше.
    StringWave wa(80);
    wa.setFarEnd(StringWave::FarEnd::Absorbing);
    wa.setDrive(0.02, 1.0);
    wa.advance(60 * stepsPerPeriod);
    auto ampA = perPointAmplitude(wa, 8, stepsPerPeriod);
    double aMaxA = 0.0, aMinA = 1e300;
    for (int i = 1; i < wa.n() - 1; ++i) {
        aMaxA = std::max(aMaxA, ampA[i]);
        aMinA = std::min(aMinA, ampA[i]);
    }
    ASSERT_GT(aMaxA, 1e-6) << "бегущая волна не раскачалась";

    // Контраст амплитуд (max/min) у стоячей значимо выше, чем у бегущей.
    double contrastStanding = aMax / std::max(aMin, 1e-12);
    double contrastTravel   = aMaxA / std::max(aMinA, 1e-12);
    EXPECT_GT(contrastStanding, 2.0 * contrastTravel)
        << "стоячая=" << contrastStanding << " бегущая=" << contrastTravel;
}

TEST(StringWave, AbsorbingEndDoesNotReflect) {
    // Поглощающий конец: даём импульс-пакет, потом снимаем драйв.
    // Энергия (через maxAbs) должна убыть — волна уходит без отражения,
    // в отличие от закреплённого конца, где она остаётся (отражается).
    StringWave wAbs(120);
    wAbs.setFarEnd(StringWave::FarEnd::Absorbing);
    wAbs.setDrive(0.02, 1.0);
    // Короткая накачка (несколько периодов), потом тишина.
    wAbs.advance(5 * 50);
    wAbs.setDrive(0.02, 0.0); // снять «руку»
    // Дать волне дойти до конца и уйти (длина 120, C=0.5 → ~240 шагов на проход).
    wAbs.advance(600);
    double residualAbs = wAbs.maxAbs();

    // Та же история с закреплённым концом — волна отражается и остаётся.
    StringWave wFix(120);
    wFix.setFarEnd(StringWave::FarEnd::Fixed);
    wFix.setDrive(0.02, 1.0);
    wFix.advance(5 * 50);
    wFix.setDrive(0.02, 0.0);
    wFix.advance(600);
    double residualFix = wFix.maxAbs();

    EXPECT_TRUE(std::isfinite(residualAbs));
    // У поглощающего конца к концу остаётся мало (волна ушла). Порог с запасом:
    // драйв был amp=1, после ухода волны остаточная рябь должна быть заметно меньше.
    EXPECT_LT(residualAbs, 0.4)
        << "поглощение слабое: residual=" << residualAbs;
    // И существенно меньше, чем у отражающего закреплённого конца.
    EXPECT_LT(residualAbs, 0.5 * residualFix)
        << "abs=" << residualAbs << " fix=" << residualFix;
}
