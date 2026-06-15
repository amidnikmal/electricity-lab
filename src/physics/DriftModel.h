#pragma once

#include "math/Vec2.h"
#include "physics/PhysicalUnits.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace current_lab::physics {

struct DriftParticleState {
    Vec2 position;
    double axialT = 0.0;
    double lateralOffset = 0.0;
};

struct DriftSamplingConfig {
    double wireThickness = 8.0;
    double cameraScale = 1.0;
    double time = 0.0;
    double visualSpeedMultiplier = kDefaultVisualDriftSpeedMultiplier;
    int componentId = 0;
    bool electronFlowVisualization = false;
};

struct DriftVisualizationInfo {
    Vec2 conventionalCurrentDirection;
    Vec2 electronDriftDirection;
    Vec2 visualDirection;
    double computedCurrent = 0.0;
    double visualSpeedMultiplier = kDefaultVisualDriftSpeedMultiplier;
    bool hasThermalMotion = true;
};

// Particle radius in WORLD units: particles zoom together with the wire
// instead of staying a fixed pixel size.
inline double particleWorldRadius(double wireThickness) {
    return std::max(0.8, wireThickness * 0.16);
}

inline int driftParticleCount(double length, double wireThickness) {
    double volume = length * wireThickness * wireThickness;
    return std::max(8, std::min(400, static_cast<int>(volume / 110.0)));
}

inline Vec2 conventionalCurrentDirection(Vec2 a, Vec2 b, double current) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len <= kMinimumPhysicalLength || std::abs(current) <= 1e-12)
        return Vec2();
    Vec2 unit = ab / len;
    return current >= 0.0 ? unit : (unit * -1.0);
}

inline DriftVisualizationInfo driftVisualizationInfo(Vec2 a,
                                                     Vec2 b,
                                                     double current,
                                                     bool electronFlowVisualization,
                                                     double visualSpeedMultiplier =
                                                         kDefaultVisualDriftSpeedMultiplier) {
    DriftVisualizationInfo info;
    info.computedCurrent = current;
    info.visualSpeedMultiplier = visualSpeedMultiplier;
    info.conventionalCurrentDirection = conventionalCurrentDirection(a, b, current);
    info.electronDriftDirection = info.conventionalCurrentDirection * -1.0;
    info.visualDirection = electronFlowVisualization
        ? info.electronDriftDirection
        : info.conventionalCurrentDirection;
    return info;
}

// Скорость дрейфа для визуализации. Строго пропорциональна |I|:
// v_d = I/(nAe); при I=0 — РОВНО ноль (контракт «нет тока — нет дрейфа»).
// physicalDriftScale ∝ плотности тока j = I/A, где A ∝ wireThickness².
// Визуальный множитель (kDriftVisualScale, visualSpeedMultiplier) отделён от
// физической пропорциональности — он лишь масштабирует картинку, не добавляя
// постоянного «пола». Нет аддитивного слагаемого ⇒ driftSpeed(0)==0.
inline double driftSpeed(double absI, const DriftSamplingConfig& config) {
    double physicalDriftScale = absI / (config.wireThickness * config.wireThickness);
    return physicalDriftScale * kDriftVisualScale
         * std::max(0.0, config.visualSpeedMultiplier);
}

inline std::vector<DriftParticleState> sampleDriftParticles(Vec2 a,
                                                            Vec2 b,
                                                            double current,
                                                            const DriftSamplingConfig& config) {
    std::vector<DriftParticleState> particles;

    double absI = std::abs(current);
    Vec2 dir = b - a;
    double len = dir.length();
    if (absI <= 1e-12 || len <= 1.0)
        return particles;

    Vec2 unit = dir / len;
    Vec2 perp(-unit.y, unit.x);

    double visualSign = (current < 0.0) ? -1.0 : 1.0;
    if (config.electronFlowVisualization)
        visualSign = -visualSign;

    double phase = std::fmod(config.time * driftSpeed(absI, config), 1.0);

    // Тепловой «шум» — детерминированные периодические функции (синтетический,
    // для воспроизводимости и производительности; не Brownian motion).

    double halfW = config.wireThickness * 0.5;
    double maxOff = std::max(0.0, halfW - particleWorldRadius(config.wireThickness) - 0.3);

    int count = driftParticleCount(len, config.wireThickness);
    particles.reserve(count);

    for (int i = 0; i < count; ++i) {
        double seed = static_cast<double>(i) * 2.718281828 +
                      static_cast<double>(config.componentId) * 1.618033989;
        double baseT = std::fmod(seed * 0.127, 1.0);
        double y0 = std::fmod(seed * 0.371, 1.0) * 2.0 * maxOff - maxOff;

        double t = baseT + visualSign * phase;
        if (t > 1.0) t -= 1.0;
        if (t < 0.0) t += 1.0;

        double thx = std::sin(config.time * 117.3 + seed * 7.1) * 2.8
                   + std::cos(config.time * 89.7 + seed * 11.3) * 2.2
                   + std::sin(config.time * 143.1 + seed * 3.7) * 1.5;
        double thy = std::cos(config.time * 103.7 + seed * 13.1) * 2.8
                   + std::sin(config.time * 127.9 + seed * 5.3) * 2.2
                   + std::cos(config.time * 77.1 + seed * 17.3) * 1.5;

        double thermalX = thx / std::max(0.05, config.cameraScale);
        double thermalY = thy / std::max(0.05, config.cameraScale);

        double oy = y0 + thermalY;
        if (oy > maxOff) oy = maxOff - (oy - maxOff) * 0.3;
        if (oy < -maxOff) oy = -maxOff + (-maxOff - oy) * 0.3;

        double tx = t + thermalX / len;
        tx = std::clamp(tx, 0.0, 1.0);

        particles.push_back(DriftParticleState{
            a + dir * tx + perp * oy,
            tx,
            oy,
        });
    }

    return particles;
}

} // namespace current_lab::physics
