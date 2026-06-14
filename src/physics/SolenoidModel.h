#pragma once

#include "math/Vec2.h"
#include "physics/PhysicalUnits.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Качественная модель магнитного поля соленоида (катушки индуктивности).
// Внутри — однородное поле B = μ₀ N I / l (параллельные оси стрелки).
// Снаружи — дипольное поле, спадающее как 1/r³.
namespace current_lab::physics {

struct SolenoidFieldSample {
    Vec2 position;
    Vec2 direction; // единичный вектор направления B
    double magnitude = 0.0;
    bool inside = false; // true — стрелка внутри соленоида
};

struct SolenoidGeometry {
    Vec2 center;
    Vec2 axis;       // единичный вектор вдоль оси (от coilStart к coilEnd)
    double halfLength = 0.0;
    double coilRadius = 0.0;
};

// B₀ = μ₀ N I / l — поле внутри идеального соленоида.
inline double solenoidInternalB(double inductanceHenry, double current,
                                double length, double turnsEstimate = 20.0) {
    if (length <= kMinimumPhysicalLength || std::abs(current) <= 1e-12)
        return 0.0;
    return kMu0 * turnsEstimate * std::abs(current) / length;
}

// Поле соленоида в точке p (качественное: внутри — константа,
// снаружи — диполь ∼ 1/r³ с магнитным моментом m = N·I·π·R²).
inline Vec2 solenoidFieldAt(Vec2 p, const SolenoidGeometry& geom,
                            double B0) {
    Vec2 r = p - geom.center;
    double axial = r.x * geom.axis.x + r.y * geom.axis.y; // проекция на ось
    double perp = (r - geom.axis * axial).length();        // расстояние от оси

    bool inside = (std::abs(axial) <= geom.halfLength * 0.95)
               && (perp <= geom.coilRadius * 0.9);

    if (inside)
        return geom.axis * B0;

    // Дипольное приближение снаружи:
    // B = (μ₀/(4π)) * (3(m·r̂)r̂ - m) / r³,
    // m = m_vec = geom.axis * (N*I*π*R²),
    // но здесь используем упрощённую форму: знаем B₀ внутри,
    // нормируем так, чтобы на полюсах (торцах) поле было B₀/2.
    double dist = r.length();
    if (dist < geom.coilRadius * 1.1)
        dist = geom.coilRadius * 1.1; // избегаем сингулярности на границе

    double axialNorm = axial / std::max(dist, 1e-9); // cos φ
    double decay = geom.halfLength * geom.halfLength * geom.halfLength
                   / (dist * dist * dist); // ∼ (l/r)³

    // Радиальная и аксиальная компоненты диполя:
    // Br ∝ 2 cos φ / r³, Bθ ∝ sin φ / r³
    double cosPhi = axialNorm;
    double sinPhi = std::sqrt(std::max(0.0, 1.0 - cosPhi * cosPhi));
    double Br = B0 * geom.halfLength * 2.0 * cosPhi * decay;
    double Btheta = B0 * geom.halfLength * sinPhi * decay;

    Vec2 rHat = r / dist;
    Vec2 thetaHat(-rHat.y, rHat.x); // перпендикуляр к радиальному
    // Знак: чтобы поле выходило из северного полюса (axis direction)
    // и входило в южный.
    if (axial < 0) {
        Br = -Br;
        Btheta = -Btheta;
    }

    return rHat * Br + thetaHat * Btheta;
}

// Сэмплирует поле соленоида для визуализации:
// — внутри: ряд параллельных стрелок вдоль оси;
// — снаружи: точки на эллипсоидальной оболочке вокруг катушки.
inline std::vector<SolenoidFieldSample> sampleSolenoidField(
    const SolenoidGeometry& geom, double current,
    double wireThickness, double cameraScale,
    double maxI) {

    std::vector<SolenoidFieldSample> samples;
    if (std::abs(current) <= 1e-12) return samples;

    double I_abs = std::abs(current);
    double B0 = solenoidInternalB(0.1, current, geom.halfLength * 2.0); // ~L оценка
    if (B0 <= 1e-18) B0 = I_abs * 0.5; // запасной масштаб

    // --- Внутренние стрелки ---
    int axialSteps = std::max(2, static_cast<int>(geom.halfLength * 2.0 / 24.0));
    int perpSteps = 2;
    for (int ai = 0; ai <= axialSteps; ++ai) {
        double t = -1.0 + 2.0 * ai / axialSteps;
        Vec2 base = geom.center + geom.axis * (geom.halfLength * t * 0.8);
        for (int pi = -perpSteps; pi <= perpSteps; ++pi) {
            if (pi == 0 && perpSteps > 0) continue;
            Vec2 pos = base + Vec2(-geom.axis.y, geom.axis.x) *
                       (geom.coilRadius * 0.35 * pi / perpSteps);
            samples.push_back({pos, geom.axis, B0, true});
        }
    }

    // --- Внешние точки (диполь) ---
    double scale = std::max(0.05, cameraScale);
    int outerRings = 3;
    int ptsPerRing = 16;
    double rStart = geom.halfLength * 1.4;
    double rEnd = geom.halfLength * 4.5;
    for (int ring = 0; ring < outerRings; ++ring) {
        double dist = rStart + (rEnd - rStart) * ring / (outerRings - 1);
        for (int pt = 0; pt < ptsPerRing; ++pt) {
            double angle = 2.0 * kPi * pt / ptsPerRing;
            Vec2 pos = geom.center + Vec2(std::cos(angle), std::sin(angle)) * dist;
            Vec2 field = solenoidFieldAt(pos, geom, B0);
            double mag = field.length();
            if (mag < 1e-12) continue;
            samples.push_back({pos, field / mag, mag, false});
        }
    }

    return samples;
}

} // namespace current_lab::physics
