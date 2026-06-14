#pragma once

#include "math/Vec2.h"
#include "physics/PhysicalUnits.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace current_lab::physics {

struct SurfaceChargeSample {
    Vec2 topPosition;
    Vec2 bottomPosition;
    double signedStrength = 0.0;
    double displayStrength = 0.0;
};

struct SurfaceChargeSamplingConfig {
    double wireThickness = 8.0;
    double cameraScale = 1.0;
};

inline std::vector<SurfaceChargeSample> sampleSurfaceCharges(Vec2 a,
                                                             Vec2 b,
                                                             double vA,
                                                             double vB,
                                                             double vMin,
                                                             double vMax,
                                                             const SurfaceChargeSamplingConfig& config) {
    std::vector<SurfaceChargeSample> samples;

    Vec2 ab = b - a;
    double len = ab.length();
    if (len <= 1.0 || config.wireThickness * config.cameraScale < 1.0)
        return samples;

    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    double dV = vB - vA;
    double vAvg = (vA + vB) * 0.5;
    double vSwing = std::max(std::abs(dV), 1e-9);

    double edgeOffset = config.wireThickness * 0.5 * 0.92;
    // Cap 64 (было 200): эти точки заряда декоративны, дробить мельче глаз не
    // видит, а при зуме len*cameraScale плодило до 200 сэмплов × 2 кружка на
    // проводник (тот же класс роста с зумом, что градиент/круги).
    int count = std::max(8, std::min(64, static_cast<int>(len * config.cameraScale / 4.0)));
    double segmentLen = len / count;

    for (int i = 0; i <= count; ++i) {
        double t = static_cast<double>(i) / count;
        double v = linearPotentialAt(vA, vB, t);
        double sigma = (v - vAvg) / vSwing;
        double absSigma = std::abs(sigma);
        if (absSigma < 0.05) continue;

        // Усиление по 2-й производной убрано: при линейном потенциале
        // лапласиан тождественно 0, блок не давал эффекта.
        double totalStrength = std::min(1.0, absSigma * 1.2);
        Vec2 center = a + unit * (len * t);

        samples.push_back(SurfaceChargeSample{
            center + perp * edgeOffset,
            center - perp * edgeOffset,
            sigma,
            totalStrength,
        });
    }

    return samples;
}

} // namespace current_lab::physics
