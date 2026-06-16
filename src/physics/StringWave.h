#pragma once
//
// StringWave — 1D поперечная волна на верёвке/струне (правая панель-аналогия).
//
// Решает одномерное волновое уравнение методом явной «чехарды» (leapfrog):
//
//     ∂²y/∂t² = c² ∂²y/∂x²
//
// Дискретизация центральными разностями второго порядка по времени и пространству:
//
//     y_new[i] = 2·y[i] − y_prev[i] + C²·(y[i+1] − 2·y[i] + y[i−1]),
//
// где C = c·dt/dx — число Куранта. Для устойчивости нужно C ≤ 1; берём C = 0.5
// с запасом. Скорость c, dx и dt поглощены в безразмерный C — внутренняя модель
// безразмерна, амплитуды порядка единицы.
//
// Ближний конец (i = 0) — «рука»: жёстко задаётся синусоидальным драйвом
// amp·sin(2π·freq·t). Дальний конец (i = n−1) — три режима граничного условия:
//   Fixed     — закреплён, y = 0 (отражение с инверсией → стоячая волна);
//   Free      — свободный, ∂y/∂x = 0 (отражение без инверсии);
//   Absorbing — условие Мура 1-го порядка (волна уходит без отражения).
//
// Чистый класс без UI/GL и без аллокаций в advance(). Покрыт tests/test_string_wave.cpp.

#include <vector>
#include <cmath>
#include <algorithm>

namespace current_lab::physics {

class StringWave {
public:
    enum class FarEnd { Fixed, Free, Absorbing };

    // n — число точек вдоль верёвки (включая оба конца). Минимум 3.
    explicit StringWave(int n)
        : n_(std::max(3, n)),
          y_(static_cast<size_t>(std::max(3, n)), 0.0),
          yPrev_(static_cast<size_t>(std::max(3, n)), 0.0),
          yNext_(static_cast<size_t>(std::max(3, n)), 0.0) {}

    // Сброс в покой: верёвка плоская, время обнулено.
    void reset() {
        std::fill(y_.begin(), y_.end(), 0.0);
        std::fill(yPrev_.begin(), yPrev_.end(), 0.0);
        std::fill(yNext_.begin(), yNext_.end(), 0.0);
        t_ = 0.0;
    }

    // Параметры колебания ближнего конца (i = 0). freq в «Гц» внутренней шкалы.
    void setDrive(double freqHz, double amp) { freq_ = freqHz; amp_ = amp; }

    void setFarEnd(FarEnd end) { farEnd_ = end; }

    // Шагнуть на substeps внутренних шагов dt. Без аллокаций.
    void advance(int substeps) {
        for (int s = 0; s < substeps; ++s) stepOnce();
    }

    // Поперечное смещение точки i, O(1).
    double y(int i) const { return y_[static_cast<size_t>(i)]; }

    int n() const { return n_; }
    double time() const { return t_; }

    // Максимум |y| по всей верёвке (для UI/тестов).
    double maxAbs() const {
        double m = 0.0;
        for (double v : y_) m = std::max(m, std::fabs(v));
        return m;
    }

private:
    // Один шаг чехарды dt_. Порядок: считаем yNext_ во внутренних точках,
    // ставим граничные условия, потом сдвигаем (prev <- y <- next).
    void stepOnce() {
        const double C2 = kCourant * kCourant;
        const int last = n_ - 1;

        // Внутренние точки 1..last-1: явная схема.
        for (int i = 1; i < last; ++i) {
            yNext_[i] = 2.0 * y_[i] - yPrev_[i]
                      + C2 * (y_[i + 1] - 2.0 * y_[i] + y_[i - 1]);
        }

        // Ближний конец — драйв «рукой». Время берём на конце шага (t_ + dt_).
        const double tNew = t_ + dt_;
        yNext_[0] = amp_ * std::sin(2.0 * kPi * freq_ * tNew);

        // Дальний конец.
        switch (farEnd_) {
            case FarEnd::Fixed:
                yNext_[last] = 0.0;
                break;
            case FarEnd::Free:
                // ∂y/∂x = 0: дальняя точка повторяет соседа (на новом слое).
                yNext_[last] = yNext_[last - 1];
                break;
            case FarEnd::Absorbing:
                // Условие Мура 1-го порядка для уходящей волны:
                //   y_new[N] = y_cur[N-1] + (C-1)/(C+1)·(y_new[N-1] - y_cur[N]),
                // где y_cur — текущий слой (y_), y_new — считаемый (yNext_).
                yNext_[last] = y_[last - 1]
                             + ((kCourant - 1.0) / (kCourant + 1.0))
                               * (yNext_[last - 1] - y_[last]);
                break;
        }

        // Сдвиг слоёв без аллокаций: prev <- cur, cur <- next.
        std::swap(yPrev_, y_);
        std::swap(y_, yNext_);
        // Теперь y_ = новый слой, yPrev_ = старый, yNext_ = «мусор» (бывший prev) —
        // он будет перезаписан на следующем шаге.

        t_ = tNew;
    }

    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kCourant = 0.5; // C = c·dt/dx, запас по устойчивости
    static constexpr double dt_ = 1.0;       // безразмерный шаг времени

    int n_;
    std::vector<double> y_, yPrev_, yNext_;
    double t_ = 0.0;
    double freq_ = 0.0;
    double amp_ = 0.0;
    FarEnd farEnd_ = FarEnd::Fixed;
};

} // namespace current_lab::physics
