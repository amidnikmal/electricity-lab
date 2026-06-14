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

// Закон Био–Савара для КОНЕЧНОГО прямолинейного отрезка a→b в точке P,
// отстоящей на расстояние r от прямой (r — перпендикулярное расстояние).
// B = (μ₀ |I| / (4π r)) · (sin θ₁ + sin θ₂),
// где θ₁,θ₂ — углы между перпендикуляром и направлениями на концы отрезка.
inline double biotSavartFiniteSegment(double current, double radius,
                                      double distToA, double distToB) {
    if (std::abs(current) <= 1e-12 || radius <= kMinimumPhysicalLength)
        return 0.0;
    double sin1 = distToA / std::sqrt(radius * radius + distToA * distToA);
    double sin2 = distToB / std::sqrt(radius * radius + distToB * distToB);
    return kMu0 * std::abs(current) / (4.0 * kPi * radius) * (sin1 + sin2);
}

// Устаревшая формула бесконечного провода — оставлена для обратной
// совместимости, но в sampleMagneticField больше не используется.
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

    // 5 радиусов сэмплирования с видимым спадом ~1/r:
    // альфа и размер глифа убывают с ростом радиуса.
    double radii[5] = {
        config.wireThickness * 0.75,
        config.wireThickness * 1.15,
        config.wireThickness * 1.80,
        config.wireThickness * 2.80,
        config.wireThickness * 4.50,
    };
    // Альфа-множители для каждого радиуса (ближе = ярче, дальше = тусклее).
    double alphas[5] = {1.0, 0.85, 0.55, 0.30, 0.12};
    int sides[2] = {1, -1};

    for (int i = 0; i < count; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(count - 1);
        Vec2 center = a + ab * t;

        // Расстояния от проекции точки на провод до концов отрезка.
        double dA = t * len;       // расстояние от foot до a
        double dB = (1.0 - t) * len; // расстояние от foot до b

        for (int ri = 0; ri < 5; ++ri) {
            double radius = radii[ri];
            for (int side : sides) {
                PageDirection direction =
                    (current * static_cast<double>(side) >= 0.0)
                        ? PageDirection::OutOfPage
                        : PageDirection::IntoPage;

                double mag = biotSavartFiniteSegment(current, radius, dA, dB);

                samples.push_back(MagneticFieldSample{
                    center + perp * (radius * static_cast<double>(side)),
                    mag,
                    radius,
                    direction,
                });
            }
        }
    }

    return samples;
}

} // namespace current_lab::physics
