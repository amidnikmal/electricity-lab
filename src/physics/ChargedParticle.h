#pragma once
//
// ChargedParticle — движение заряда в электрическом и магнитном поле (сила Лоренца).
//
//     m dv/dt = q (E + v × B)
//
// 2D-приложение: B перпендикулярна плоскости (скаляр Bz), v и E лежат в плоскости.
// Интегратор — схема Бориса (Boris pusher): магнитный поворот делается ОТДЕЛЬНО от
// электрического толчка, поэтому в чистом B энергия сохраняется ТОЧНО (не растёт и
// не затухает за много оборотов) — это и есть «честный» интегратор для циклотронных
// орбит. Чистый заголовок, без UI/GL; покрыт tests/test_charged_particle.cpp.
//
// Базовый модуль для учебной песочницы «сила Лоренца» (циклотрон, дрейф E×B,
// селектор скоростей) — см. docs/RESEARCH_MAGNETISM_MODULES_2026-06-16.md (этап M1).

#include "math/Vec2.h"
#include <cmath>

namespace current_lab::physics {

struct ParticleState {
    Vec2 pos;
    Vec2 vel;
};

// Один шаг схемы Бориса. q — заряд, m — масса (>0), E — поле в плоскости,
// Bz — компонента B вне плоскости, dt — шаг времени.
inline ParticleState borisStep(const ParticleState& s, double q, double m,
                               Vec2 E, double Bz, double dt) {
    double qm = q / (m > 1e-30 ? m : 1e-30);

    // 1) половинный электрический толчок
    Vec2 vMinus = s.vel + E * (qm * dt * 0.5);

    // 2) поворот в магнитном поле (вокруг оси z): v' = v⁻ + v⁻×t, v⁺ = v⁻ + v'×s
    //    для B = Bz·ẑ:  a×ẑ·t = (a_y·t, −a_x·t)
    double t = qm * Bz * dt * 0.5;
    Vec2 vPrime{ vMinus.x + vMinus.y * t, vMinus.y - vMinus.x * t };
    double ss = 2.0 * t / (1.0 + t * t);
    Vec2 vPlus{ vMinus.x + vPrime.y * ss, vMinus.y - vPrime.x * ss };

    // 3) второй половинный электрический толчок
    ParticleState out;
    out.vel = vPlus + E * (qm * dt * 0.5);
    out.pos = s.pos + out.vel * dt;
    return out;
}

// Циклотронные величины (для тестов/подписей).
inline double cyclotronRadius(double m, double speed, double q, double Bz) {
    double denom = std::abs(q * Bz);
    return denom > 1e-30 ? m * speed / denom : 0.0;
}
inline double cyclotronAngularFreq(double q, double m, double Bz) {
    return (m > 1e-30) ? q * Bz / m : 0.0;
}
// Дрейф направляющего центра в скрещённых полях: v = E×B / B².
inline Vec2 exbDrift(Vec2 E, double Bz) {
    if (std::abs(Bz) < 1e-30) return Vec2();
    // E×(Bz ẑ) = (E_y·Bz, −E_x·Bz);  делим на B² = Bz²
    return Vec2{ E.y * Bz, -E.x * Bz } * (1.0 / (Bz * Bz));
}

} // namespace current_lab::physics
