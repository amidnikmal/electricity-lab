#include <gtest/gtest.h>
#include "render/LICFieldRenderer.h"
#include "render/ColorMaps.h"
#include <cmath>
#include <cstdint>

namespace {

using namespace current_lab::render;
using namespace current_lab::render::lic;

int chR(uint32_t c) { return static_cast<int>(c & 0xFF); }
int chG(uint32_t c) { return static_cast<int>((c >> 8) & 0xFF); }
int chB(uint32_t c) { return static_cast<int>((c >> 16) & 0xFF); }
int chA(uint32_t c) { return static_cast<int>((c >> 24) & 0xFF); }

// Constant horizontal field: E = (1, 0) everywhere.
Vec2 constField(Vec2 /*p*/) {
    return Vec2(1.0, 0.0);
}

// Zero field everywhere.
Vec2 zeroField(Vec2 /*p*/) {
    return Vec2(0.0, 0.0);
}

} // namespace

// Determinism: same seed + same field → identical LIC output.
TEST(LIC, Determinism) {
    LICConfig config;
    config.gridW = 16;
    config.gridH = 12;
    config.seed = 42;
    config.streamlineSteps = 8;
    config.stepSize = 1.0;

    auto r1 = computeLIC(constField, Vec2(0, 0), Vec2(10, 10), config);
    auto r2 = computeLIC(constField, Vec2(0, 0), Vec2(10, 10), config);

    ASSERT_EQ(r1.pixels.size(), r2.pixels.size());
    for (size_t i = 0; i < r1.pixels.size(); ++i)
        EXPECT_EQ(r1.pixels[i], r2.pixels[i]);
}

// Different seed → different output.
TEST(LIC, DifferentSeedDifferentOutput) {
    LICConfig config;
    config.gridW = 16;
    config.gridH = 12;
    config.streamlineSteps = 8;
    config.stepSize = 1.0;

    config.seed = 42;
    auto r1 = computeLIC(constField, Vec2(0, 0), Vec2(10, 10), config);
    config.seed = 99;
    auto r2 = computeLIC(constField, Vec2(0, 0), Vec2(10, 10), config);

    ASSERT_EQ(r1.pixels.size(), r2.pixels.size());
    int diffCount = 0;
    for (size_t i = 0; i < r1.pixels.size(); ++i)
        if (r1.pixels[i] != r2.pixels[i]) ++diffCount;
    EXPECT_GT(diffCount, 0);
}

// All output pixels must have valid RGBA channels (0-255).
TEST(LIC, FiniteValues) {
    LICConfig config;
    config.gridW = 24;
    config.gridH = 18;
    config.seed = 42;
    config.streamlineSteps = 8;
    config.stepSize = 1.5;
    config.alpha = 200;

    auto result = computeLIC(constField, Vec2(-5, -5), Vec2(15, 15), config);

    EXPECT_EQ(result.w, 24);
    EXPECT_EQ(result.h, 18);
    EXPECT_EQ(result.pixels.size(), 24u * 18u);

    for (auto c : result.pixels) {
        int r = chR(c);
        int g = chG(c);
        int b = chB(c);
        int a = chA(c);
        EXPECT_GE(r, 0); EXPECT_LE(r, 255);
        EXPECT_GE(g, 0); EXPECT_LE(g, 255);
        EXPECT_GE(b, 0); EXPECT_LE(b, 255);
        EXPECT_GE(a, 0); EXPECT_LE(a, 255);
    }
}

// LIC with zero field: all streamlines collapse, output should still be valid
// (pixels should sample their own noise cell with no averaging).
TEST(LIC, ZeroFieldProducesValidOutput) {
    LICConfig config;
    config.gridW = 10;
    config.gridH = 8;
    config.seed = 42;
    config.streamlineSteps = 8;
    config.stepSize = 1.0;

    auto result = computeLIC(zeroField, Vec2(0, 0), Vec2(10, 10), config);

    EXPECT_EQ(result.pixels.size(), 10u * 8u);
    for (auto c : result.pixels) {
        int a = chA(c);
        EXPECT_GT(a, 0);
    }
}

// Constant horizontal field: LIC vertical cross-section should show variation
// For a constant horizontal field, streamlines run purely horizontally, so
// adjacent pixels in the same row integrate nearly identical noise samples.
// Verify horizontal correlation via the raw LIC values (before colormap).
TEST(LIC, ConstantFieldProducesNonTrivialPattern) {
    LICConfig config;
    config.gridW = 64;
    config.gridH = 8;
    config.seed = 42;
    config.streamlineSteps = 48;
    config.stepSize = 1.0;

    auto noise = generateNoise(config.gridW, config.gridH, config.seed);
    Vec2 worldMin(0, 0), worldMax(64, 8);

    // Compute raw LIC values along row 4.
    std::vector<double> rawRow(static_cast<size_t>(config.gridW));
    for (int i = 0; i < config.gridW; ++i) {
        rawRow[static_cast<size_t>(i)] = convolveStreamline(
            noise, config.gridW, config.gridH,
            constField, worldMin, worldMax,
            static_cast<double>(i), 4.0,
            config.streamlineSteps, config.stepSize);
    }

    // Not all identical — LIC pattern has variation.
    bool allSame = true;
    for (size_t i = 1; i < rawRow.size(); ++i)
        if (std::abs(rawRow[i] - rawRow[0]) > 1e-9) { allSame = false; break; }
    EXPECT_FALSE(allSame) << "LIC output should vary along the field direction";

    // Pearson correlation between rawRow[i] and rawRow[i-1].
    double mean1 = 0, mean2 = 0;
    for (size_t i = 1; i < rawRow.size(); ++i) {
        mean1 += rawRow[i];
        mean2 += rawRow[i - 1];
    }
    size_t n = rawRow.size() - 1;
    mean1 /= static_cast<double>(n);
    mean2 /= static_cast<double>(n);

    double cov = 0, var1 = 0, var2 = 0;
    for (size_t i = 1; i < rawRow.size(); ++i) {
        double d1 = rawRow[i] - mean1;
        double d2 = rawRow[i - 1] - mean2;
        cov += d1 * d2;
        var1 += d1 * d1;
        var2 += d2 * d2;
    }

    double corr = cov / std::sqrt(var1 * var2);
    EXPECT_GT(corr, 0.75) << "Adjacent horizontal values should be strongly correlated for a constant horizontal field (got " << corr << ")";
}

// Check that the contrast stretch function maps inputs reasonably.
TEST(LIC, ContrastStretch) {
    EXPECT_NEAR(contrastStretch(0.0), 0.0025, 0.01);
    EXPECT_NEAR(contrastStretch(0.5), 0.5, 0.01);
    EXPECT_NEAR(contrastStretch(1.0), 0.9975, 0.01);
    // Monotonic
    EXPECT_LT(contrastStretch(0.0), contrastStretch(0.5));
    EXPECT_LT(contrastStretch(0.5), contrastStretch(1.0));
}

// Check noise generation is deterministic with same seed.
TEST(LIC, NoiseDeterminism) {
    auto n1 = generateNoise(16, 12, 42);
    auto n2 = generateNoise(16, 12, 42);
    ASSERT_EQ(n1.size(), n2.size());
    for (size_t i = 0; i < n1.size(); ++i)
        EXPECT_DOUBLE_EQ(n1[i], n2[i]);
}

// Check noise values are in [0, 1].
TEST(LIC, NoiseRange) {
    auto n = generateNoise(32, 24, 12345);
    for (auto v : n) {
        EXPECT_GE(v, 0.0);
        EXPECT_LE(v, 1.0);
    }
}
