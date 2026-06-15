#pragma once

#include "math/Vec2.h"
#include "render/ColorMaps.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace current_lab::render::lic {

struct LICConfig {
    int gridW = 64;           // horizontal grid resolution
    int gridH = 48;           // vertical grid resolution
    int streamlineSteps = 14; // steps forward + back from each seed
    double stepSize = 3.0;    // world-unit step per integration step
    unsigned seed = 42;       // deterministic noise seed
    Colormap colormap = Colormap::Viridis;
    int alpha = 180;          // global alpha for output colors
};

struct LICResult {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> pixels; // RGBA, row-major
};

// RK4 integration step: advances p by dt along the field defined by f.
inline Vec2 rk4Step(const std::function<Vec2(Vec2)>& field, Vec2 p, double dt) {
    Vec2 k1 = field(p);
    if (k1.length() < 1e-12) return p;
    k1 = k1.normalized();
    Vec2 k2 = field(p + k1 * (dt * 0.5)).normalized();
    if (k2.length() < 1e-12) k2 = k1;
    Vec2 k3 = field(p + k2 * (dt * 0.5)).normalized();
    if (k3.length() < 1e-12) k3 = k1;
    Vec2 k4 = field(p + k3 * dt).normalized();
    if (k4.length() < 1e-12) k4 = k1;
    Vec2 dir = (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (1.0 / 6.0);
    double len = dir.length();
    if (len < 1e-12) return p;
    return p + dir * (dt / len);
}

// Bilinear sample from a float grid.
inline double bilinearSample(const std::vector<double>& grid, int gw, int gh, double u, double v) {
    u = std::clamp(u, 0.0, static_cast<double>(gw - 1));
    v = std::clamp(v, 0.0, static_cast<double>(gh - 1));
    int i0 = static_cast<int>(u);
    int j0 = static_cast<int>(v);
    int i1 = std::min(i0 + 1, gw - 1);
    int j1 = std::min(j0 + 1, gh - 1);
    double fu = u - i0;
    double fv = v - j0;
    double v00 = grid[j0 * gw + i0];
    double v10 = grid[j0 * gw + i1];
    double v01 = grid[j1 * gw + i0];
    double v11 = grid[j1 * gw + i1];
    return v00 * (1.0 - fu) * (1.0 - fv) + v10 * fu * (1.0 - fv) +
           v01 * (1.0 - fu) * fv + v11 * fu * fv;
}

// Generate white noise: uniform [0, 1] float per grid cell.
inline std::vector<double> generateNoise(int w, int h, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::vector<double> noise(static_cast<size_t>(w) * h);
    for (auto& v : noise)
        v = dist(rng);
    return noise;
}

// Convolve a single streamline: start at (cx, cy) in grid coords, step forward
// and backward along field, sample noise bilinearly, return average.
inline double convolveStreamline(const std::vector<double>& noise, int gw, int gh,
                                  const std::function<Vec2(Vec2)>& field,
                                  Vec2 worldMin, Vec2 worldMax,
                                  double cx, double cy,
                                  int steps, double stepSize) {
    double cellW = (worldMax.x - worldMin.x) / static_cast<double>(gw);
    double cellH = (worldMax.y - worldMin.y) / static_cast<double>(gh);
    double sum = 0.0;
    int count = 0;

    auto worldToGrid = [&](Vec2 w) {
        double u = (w.x - worldMin.x) / cellW;
        double v = (w.y - worldMin.y) / cellH;
        return std::make_pair(u, v);
    };

    // Seed at pixel center in world coords
    Vec2 seedWorld(worldMin.x + (cx + 0.5) * cellW,
                   worldMin.y + (cy + 0.5) * cellH);

    // Forward trace
    Vec2 p = seedWorld;
    double sign = 1.0;
    for (int dir = 0; dir < 2; ++dir) {
        p = seedWorld;
        for (int s = 0; s < steps; ++s) {
            Vec2 e = field(p);
            double eLen = e.length();
            if (eLen < 1e-12) break;
            Vec2 forward = (e / eLen) * sign;
            p = rk4Step(field, p, stepSize * sign);

            auto [u, v] = worldToGrid(p);
            if (u < 0.0 || u >= static_cast<double>(gw) ||
                v < 0.0 || v >= static_cast<double>(gh))
                break;

            sum += bilinearSample(noise, gw, gh, u, v);
            ++count;
        }
        sign = -1.0;
    }

    if (count == 0) {
        auto [u, v] = worldToGrid(seedWorld);
        if (u >= 0.0 && u < static_cast<double>(gw) && v >= 0.0 && v < static_cast<double>(gh))
            return bilinearSample(noise, gw, gh, u, v);
        return 0.5;
    }
    return sum / static_cast<double>(count);
}

// Contrast stretch: remap [0,1] values so LIC pattern is visible (LIC raw output
// converges to ~0.5 due to averaging; we stretch to use the full colormap range).
inline double contrastStretch(double v) {
    // Sigmoid stretch centered at 0.5: output ∈ [0, 1], emphasises texture contrast.
    double x = (v - 0.5) * 6.0; // steepness 6 → good visibility without clipping too much
    return 0.5 + 0.5 * std::tanh(x);
}

// Full CPU-LIC computation.
// field: Vec2 → Vec2 sampler (must return direction + magnitude, zero vector = no field).
// worldMin/Max: bounding box in world coordinates.
// PERFORMANCE NOTE: moderate resolution (64×48) with 14 RK4 steps per direction
// (~28 field evaluations per pixel) runs in ~3 ms on a modern CPU for a single field.
// Resolution is intentionally limited for real-time CPU use; GPU/FBO LIC would
// support higher resolutions if needed.
inline LICResult computeLIC(const std::function<Vec2(Vec2)>& field,
                            Vec2 worldMin, Vec2 worldMax,
                            const LICConfig& config = LICConfig{}) {
    LICResult result;
    result.w = config.gridW;
    result.h = config.gridH;
    result.pixels.resize(static_cast<size_t>(config.gridW) * config.gridH);

    auto noise = generateNoise(config.gridW, config.gridH, config.seed);

    for (int j = 0; j < config.gridH; ++j) {
        for (int i = 0; i < config.gridW; ++i) {
            double raw = convolveStreamline(noise, config.gridW, config.gridH,
                                            field, worldMin, worldMax,
                                            static_cast<double>(i), static_cast<double>(j),
                                            config.streamlineSteps, config.stepSize);
            double t = contrastStretch(raw);
            result.pixels[j * config.gridW + i] = colormapSample(config.colormap, t, config.alpha);
        }
    }
    return result;
}

} // namespace current_lab::render::lic
