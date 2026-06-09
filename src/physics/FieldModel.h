#pragma once

#include "math/Vec2.h"
#include "physics/WirePhysics.h"
#include <algorithm>
#include <vector>

namespace current_lab::physics {

struct FieldArrowSample {
    Vec2 position;
    Vec2 direction;
    double magnitude = 0.0;
};

struct FieldSamplingConfig {
    double cameraScale = 1.0;
    double wireHalfWidth = 4.0;
    double maxMagnitude = 1.0;
};

inline Vec2 fieldDirection(Vec2 a, Vec2 b, double vA, double vB) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len <= kMinimumPhysicalLength || std::abs(vA - vB) <= 1e-12)
        return Vec2();
    Vec2 unit = ab / len;
    return (vA > vB) ? unit : (unit * -1.0);
}

inline std::vector<FieldArrowSample> sampleFieldArrows(Vec2 a,
                                                       Vec2 b,
                                                       double vA,
                                                       double vB,
                                                       const FieldSamplingConfig& config) {
    std::vector<FieldArrowSample> samples;

    Vec2 ab = b - a;
    double len = ab.length();
    double magnitude = electricFieldMagnitude(vA - vB, len);
    if (len <= 1.0 || magnitude <= 1e-12 || config.maxMagnitude <= 1e-12)
        return samples;

    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);
    Vec2 dir = fieldDirection(a, b, vA, vB);

    double arrowSpacing = 36.0 / std::max(0.05, config.cameraScale);
    arrowSpacing = std::clamp(arrowSpacing, 10.0, 80.0);

    double screenWidth = config.wireHalfWidth * 2.0 * config.cameraScale;
    int rows = 1;
    if (screenWidth > 24.0)
        rows = std::clamp(static_cast<int>(screenWidth / 20.0), 1, 5);

    int count = static_cast<int>((len - arrowSpacing * 0.5) / arrowSpacing) + 1;
    count = std::max(1, count);

    for (int row = 0; row < rows; ++row) {
        double rowOffset = 0.0;
        if (rows > 1) {
            rowOffset = -config.wireHalfWidth * 0.85 +
                        config.wireHalfWidth * 1.7 * static_cast<double>(row) /
                            static_cast<double>(rows - 1);
        }

        for (int i = 0; i < count; ++i) {
            double t = arrowSpacing * 0.5 + i * arrowSpacing;
            if (t > len) break;

            samples.push_back(FieldArrowSample{
                a + unit * t + perp * rowOffset,
                dir,
                magnitude,
            });
        }
    }

    return samples;
}

} // namespace current_lab::physics
