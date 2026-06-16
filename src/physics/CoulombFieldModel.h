#pragma once
//
// CoulombFieldModel — электростатическое поле и потенциал от точечных зарядов
// прямой суперпозицией (закон Кулона). Чистый заголовок, без UI/GL.
//
//     E(r) = k Σ qᵢ (r−rᵢ)/|r−rᵢ|³ ,   φ(r) = k Σ qᵢ/|r−rᵢ|
//
// Это «быстрый» путь к электростатике в пространстве (этап E1 дорожной карты,
// docs/RESEARCH_MAGNETISM_MODULES_2026-06-16.md): силовые линии, эквипотенциали,
// поле диполя, движение пробного заряда (вместе с ChargedParticle/Boris).
// Сеточный решатель Пуассона (проводники/диэлектрики) — отдельный больший модуль.
//
// Мягчение (softening) ε² в знаменателе убирает сингулярность у самого заряда —
// для устойчивой визуализации и интегрирования траекторий вблизи источника.

#include "math/Vec2.h"
#include <cmath>
#include <vector>

namespace current_lab::physics {

struct PointCharge {
    Vec2 pos;
    double q = 1.0;
};

struct CoulombConfig {
    double k = 1.0;          // постоянная Кулона (учебные единицы; 1.0 по умолчанию)
    double softening = 1.0;  // ε: |r|²+ε² в знаменателе (мир-единицы)
};

inline Vec2 coulombField(Vec2 p, const std::vector<PointCharge>& charges,
                         const CoulombConfig& cfg = CoulombConfig{}) {
    Vec2 e;
    double eps2 = cfg.softening * cfg.softening;
    for (const auto& c : charges) {
        Vec2 r = p - c.pos;
        double r2 = r.x * r.x + r.y * r.y + eps2;
        double invR3 = 1.0 / (r2 * std::sqrt(r2));
        e = e + r * (cfg.k * c.q * invR3);
    }
    return e;
}

inline double coulombPotential(Vec2 p, const std::vector<PointCharge>& charges,
                               const CoulombConfig& cfg = CoulombConfig{}) {
    double phi = 0.0;
    double eps2 = cfg.softening * cfg.softening;
    for (const auto& c : charges) {
        Vec2 r = p - c.pos;
        double dist = std::sqrt(r.x * r.x + r.y * r.y + eps2);
        phi += cfg.k * c.q / dist;
    }
    return phi;
}

} // namespace current_lab::physics
