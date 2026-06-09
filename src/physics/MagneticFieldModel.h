#pragma once

#include "math/Vec2.h"
#include "physics/PhysicalUnits.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace current_lab::physics {

enum class PageDirection {
    OutOfPage,
    IntoPage,
};

struct MagneticFieldSample {
    Vec2 position;
    double magnitude = 0.0;
    double radius = 0.0;
    PageDirection direction = PageDirection::OutOfPage;
};

struct MagneticFieldSamplingConfig {
    double wireThickness = 8.0;
    double cameraScale = 1.0;
};

inline double magneticFieldMagnitude(double current, double radius) {
    if (std::abs(current) <= 1e-12 || radius <= kMinimumPhysicalLength)
        return 0.0;
    return kMu0 * std::abs(current) / (2.0 * kPi * radius);
}

inline std::vector<MagneticFieldSample> sampleMagneticField(Vec2 a,
                                                            Vec2 b,
                                                            double current,
                                                            const MagneticFieldSamplingConfig& config) {
    std::vector<MagneticFieldSample> samples;

    double absI = std::abs(current);
    Vec2 ab = b - a;
    double len = ab.length();
    if (absI <= 1e-12 || len <= 1.0)
        return samples;

    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    double spacing = 60.0 / std::max(0.05, config.cameraScale);
    spacing = std::clamp(spacing, 16.0, 90.0);
    int count = std::max(2, static_cast<int>(len / spacing) + 1);

    double radii[2] = {
        config.wireThickness * 0.75,
        config.wireThickness * 1.15,
    };
    int sides[2] = {1, -1};

    for (int i = 0; i < count; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(count - 1);
        Vec2 center = a + ab * t;

        for (double radius : radii) {
            for (int side : sides) {
                PageDirection direction =
                    (current * static_cast<double>(side) >= 0.0)
                        ? PageDirection::OutOfPage
                        : PageDirection::IntoPage;

                samples.push_back(MagneticFieldSample{
                    center + perp * (radius * static_cast<double>(side)),
                    magneticFieldMagnitude(current, radius),
                    radius,
                    direction,
                });
            }
        }
    }

    return samples;
}

} // namespace current_lab::physics
