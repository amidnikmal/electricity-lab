// Тесты ядра 2D-ряби (скалярное волновое уравнение, аналогия ЭМ-волны).
// Проверяем физику, а не рендер:
//  - покой: без источника поле остаётся нулевым;
//  - оживание: точечный осциллятор рождает кольца (растёт дисперсия высот);
//  - бегущий фронт: радиус возмущения растёт со временем;
//  - две щели: барьер с проходами пропускает волну (в отличие от глухой стены);
//  - преломление: в зоне с меньшей скоростью рябь пространственно чаще.
#include <gtest/gtest.h>
#include "physics/RippleField.h"
#include <cmath>
#include <vector>

using current_lab::physics::RippleField;

namespace {

// Стандартное отклонение высот по всей сетке — мера «оживания» поля.
double heightStdDev(const RippleField& f) {
    int N = f.grid();
    double sum = 0.0, sum2 = 0.0;
    int cnt = 0;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            double h = f.height(i, j);
            sum += h; sum2 += h * h; ++cnt;
        }
    double mean = sum / cnt;
    return std::sqrt(std::max(0.0, sum2 / cnt - mean * mean));
}

// Среднее число смен знака вдоль строки i на отрезке [j0, j1) — грубая мера
// пространственной частоты ряби (короче длина волны ⇒ больше смен знака).
int signChangesAlongRow(const RippleField& f, int i, int j0, int j1) {
    int changes = 0;
    int prevSign = 0;
    for (int j = j0; j < j1; ++j) {
        double h = f.height(i, j);
        int s = (h > 1e-6) ? 1 : (h < -1e-6 ? -1 : 0);
        if (s != 0) {
            if (prevSign != 0 && s != prevSign) ++changes;
            prevSign = s;
        }
    }
    return changes;
}

} // namespace

TEST(Ripple, RestStaysZero) {
    RippleField f(80);
    f.advance(200);
    EXPECT_EQ(f.maxAbsHeight(), 0.0f);
    EXPECT_NEAR(heightStdDev(f), 0.0, 1e-9);
}

TEST(Ripple, SourceComesAlive) {
    RippleField f(120);
    int c = 60;
    f.addDrivenSource(c, c, /*freq*/0.08, /*amp*/1.0);

    double sd0 = heightStdDev(f);
    f.advance(300);
    double sd1 = heightStdDev(f);

    EXPECT_NEAR(sd0, 0.0, 1e-9);
    EXPECT_GT(sd1, 1e-3) << "поле не ожило";
    EXPECT_GT(f.maxAbsHeight(), 1e-3f);

    // Должно быть много РАЗНЫХ значений (кольца, а не плоская ступенька).
    std::vector<double> vals;
    for (int j = 0; j < f.grid(); ++j) vals.push_back(f.height(c, j));
    bool hasPos = false, hasNeg = false;
    for (double v : vals) { if (v > 1e-4) hasPos = true; if (v < -1e-4) hasNeg = true; }
    EXPECT_TRUE(hasPos && hasNeg) << "нет колебаний знака — нет колец";
}

TEST(Ripple, FrontTravelsOutward) {
    RippleField f(160);
    int c = 80;
    f.addDrivenSource(c, c, 0.10, 1.0);

    // Далёкая ячейка (по оси j) на расстоянии ~60 от источника.
    const int probe = c + 60;

    // Рано: фронт ещё не дошёл — далёкая ячейка спокойна.
    f.advance(40);
    double early = std::fabs(f.height(c, probe));

    // Поздно: фронт уже добежал — далёкая ячейка задета.
    f.advance(400);
    double late = std::fabs(f.height(c, probe));

    EXPECT_LT(early, 1e-3) << "фронт пришёл слишком рано: " << early;
    EXPECT_GT(late, 5e-3)  << "фронт не добежал: " << late;
}

TEST(Ripple, DoubleSlitLetsWaveThrough) {
    auto runWith = [](bool openSlits) {
        RippleField f(140);
        int c = 70;
        // Вертикальная стена на столбце jWall, источник слева от неё.
        const int jWall = 70;
        const int jSrc  = 40;
        f.addDrivenSource(c, jSrc, 0.10, 1.0);

        // Две щели вокруг центра по строкам.
        const int slitA = c - 12, slitB = c + 12;
        for (int i = 0; i < f.grid(); ++i) {
            bool slit = openSlits &&
                (std::abs(i - slitA) <= 2 || std::abs(i - slitB) <= 2);
            if (!slit) f.setBarrier(i, jWall, true);
        }
        f.advance(700);

        // Энергия ряби ЗА стеной (столбцы правее jWall) на центральной оси.
        double e = 0.0;
        for (int j = jWall + 3; j < f.grid() - 5; ++j) {
            double h = f.height(c, j);
            e += h * h;
        }
        return e;
    };

    double closed = runWith(false);  // глухая стена
    double open   = runWith(true);   // две щели

    EXPECT_GT(open, 4.0 * (closed + 1e-9))
        << "щели не пропускают волну: open=" << open << " closed=" << closed;
}

TEST(Ripple, SlowZoneShortensWavelength) {
    RippleField f(160);
    // ПЛОСКАЯ волна (линия источников вдоль i на j=5, бежит в +j): в отличие от
    // точечного источника, у неё нет 2D-спада амплитуды ~1/√r, поэтому в быстрой и
    // медленной зонах рябь сравнима по высоте — честное сравнение длины волны.
    // Низкая частота (λ_fast≈10 ячеек) — мало численной дисперсии.
    for (int i = 1; i < f.grid() - 1; ++i)
        f.addDrivenSource(i, 5, 0.05, 1.0);

    // Правую часть делаем «медленной» (speedScale 0.5 ⇒ λ вдвое короче).
    const int jSlow = 80;
    for (int i = 0; i < f.grid(); ++i)
        for (int j = jSlow; j < f.grid(); ++j)
            f.setSpeedScale(i, j, 0.5f);

    f.advance(800);

    // Окна одинаковой ширины по обе стороны границы; усредняем по строкам у центра.
    const int win = 40;
    int fastTotal = 0, slowTotal = 0;
    for (int i = 78; i <= 82; ++i) {
        fastTotal += signChangesAlongRow(f, i, jSlow - 5 - win, jSlow - 5);
        slowTotal += signChangesAlongRow(f, i, jSlow + 5, jSlow + 5 + win);
    }
    EXPECT_GT(slowTotal, fastTotal)
        << "в медленной зоне длина волны не короче: fast=" << fastTotal
        << " slow=" << slowTotal;
}
