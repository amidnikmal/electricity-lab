#include <gtest/gtest.h>
#include <cmath>
#include "math/LinearSystem.h"

TEST(LinearSystem, Solve1x1) {
    LinearSystem s;
    s.resize(1);
    s.A[0][0] = 3.0;
    s.b[0] = 9.0;
    auto x = s.solve();
    ASSERT_EQ(x.size(), 1u);
    EXPECT_NEAR(x[0], 3.0, 1e-12);
}

TEST(LinearSystem, Solve2x2) {
    LinearSystem s;
    s.resize(2);
    s.A = {{2, 1}, {1, 3}};
    s.b = {5, 10};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 2u);
    EXPECT_NEAR(x[0], 1.0, 1e-12);
    EXPECT_NEAR(x[1], 3.0, 1e-12);
}

TEST(LinearSystem, Solve3x3) {
    LinearSystem s;
    s.resize(3);
    s.A = {{3, 2, -1}, {2, -2, 4}, {-1, 0.5, -1}};
    s.b = {1, -2, 0};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 3u);
    EXPECT_NEAR(x[0], 1.0, 1e-10);
    EXPECT_NEAR(x[1], -2.0, 1e-10);
    EXPECT_NEAR(x[2], -2.0, 1e-10);
}

TEST(LinearSystem, Identity) {
    LinearSystem s;
    s.resize(4);
    for (int i = 0; i < 4; ++i) s.A[i][i] = 1.0;
    s.b = {7, 13, 42, -5};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 4u);
    EXPECT_NEAR(x[0], 7.0, 1e-12);
    EXPECT_NEAR(x[1], 13.0, 1e-12);
    EXPECT_NEAR(x[2], 42.0, 1e-12);
    EXPECT_NEAR(x[3], -5.0, 1e-12);
}

TEST(LinearSystem, NeedsPivoting) {
    LinearSystem s;
    s.resize(2);
    s.A = {{0, 1}, {1, 1}};
    s.b = {2, 3};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 2u);
    EXPECT_NEAR(x[0], 1.0, 1e-12);
    EXPECT_NEAR(x[1], 2.0, 1e-12);
}

TEST(LinearSystem, LargerPivotDeepInMatrix) {
    LinearSystem s;
    s.resize(4);
    s.A = {
        {1, 2, 3, 4},
        {2, 0.001, 1, 1},
        {3, 1, 0.001, 2},
        {4, 2, 1, 0.001}
    };
    s.b = {30, 10, 14, 18};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 4u);
    for (int i = 0; i < 4; ++i) {
        double sum = 0.0;
        for (int j = 0; j < 4; ++j) sum += s.A[i][j] * x[j];
        EXPECT_NEAR(sum, s.b[i], 1e-9);
    }
}

TEST(LinearSystem, AllZeroRHS) {
    LinearSystem s;
    s.resize(3);
    s.A = {{2, -1, 0}, {-1, 2, -1}, {0, -1, 2}};
    s.b = {0, 0, 0};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 3u);
    EXPECT_NEAR(x[0], 0.0, 1e-12);
    EXPECT_NEAR(x[1], 0.0, 1e-12);
    EXPECT_NEAR(x[2], 0.0, 1e-12);
}

TEST(LinearSystem, IllConditionedButSolveable) {
    LinearSystem s;
    s.resize(2);
    s.A = {{1e-10, 1}, {1, 1}};
    s.b = {1, 2};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 2u);
    double r0 = s.A[0][0] * x[0] + s.A[0][1] * x[1];
    double r1 = s.A[1][0] * x[0] + s.A[1][1] * x[1];
    EXPECT_NEAR(r0, s.b[0], 1e-8);
    EXPECT_NEAR(r1, s.b[1], 1e-8);
}

TEST(LinearSystem, ZeroRowInMatrix) {
    LinearSystem s;
    s.resize(3);
    s.A = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 1, -1}
    };
    s.b = {6, 0, 1};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 3u);
    EXPECT_NEAR(x[1] - x[2], 1.0, 1e-12);
}

TEST(LinearSystem, ResizeClearsPrevious) {
    LinearSystem s;
    s.resize(3);
    s.A[0][0] = 1; s.b[0] = 5;
    s.resize(2);
    EXPECT_EQ(s.n, 2);
    EXPECT_EQ(s.A[0][0], 0.0);
    EXPECT_EQ(s.b[0], 0.0);
}

TEST(LinearSystem, EmptySystem) {
    LinearSystem s;
    s.resize(0);
    auto x = s.solve();
    EXPECT_TRUE(x.empty());
}

TEST(LinearSystem, Hilbert3x3) {
    LinearSystem s;
    s.resize(3);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            s.A[i][j] = 1.0 / (i + j + 1);
    s.b[0] = s.A[0][0] * 1 + s.A[0][1] * 2 + s.A[0][2] * 3;
    s.b[1] = s.A[1][0] * 1 + s.A[1][1] * 2 + s.A[1][2] * 3;
    s.b[2] = s.A[2][0] * 1 + s.A[2][1] * 2 + s.A[2][2] * 3;
    auto x = s.solve();
    ASSERT_EQ(x.size(), 3u);
    EXPECT_NEAR(x[0], 1.0, 1e-8);
    EXPECT_NEAR(x[1], 2.0, 1e-8);
    EXPECT_NEAR(x[2], 3.0, 1e-8);
}

TEST(LinearSystem, NegativeValues) {
    LinearSystem s;
    s.resize(3);
    s.A = {
        {1, -2, 3},
        {-2, 5, -1},
        {3, -1, 4}
    };
    s.b = {-1, 3, 10};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 3u);
    EXPECT_NEAR(x[0], 67.0 / 15.0, 1e-10);
    EXPECT_NEAR(x[1], 7.0 / 3.0, 1e-10);
    EXPECT_NEAR(x[2], -4.0 / 15.0, 1e-10);
}

TEST(LinearSystem, LargeValues) {
    LinearSystem s;
    s.resize(2);
    s.A = {{1e6, 5}, {3, 2e6}};
    s.b = {1e6 * 2 + 5 * (-1), 3 * 2 + 2e6 * (-1)};
    auto x = s.solve();
    ASSERT_EQ(x.size(), 2u);
    EXPECT_NEAR(x[0], 2.0, 1e-9);
    EXPECT_NEAR(x[1], -1.0, 1e-9);
}

TEST(LinearSystem, DiagonallyDominant5x5) {
    LinearSystem s;
    const int N = 5;
    s.resize(N);
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            s.A[i][j] = (i == j) ? (N + 1.0) : 1.0;
        }
        s.b[i] = (i + 1.0) * N;
    }
    auto x = s.solve();
    ASSERT_EQ(x.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        double sum = 0.0;
        for (int j = 0; j < N; ++j) sum += s.A[i][j] * x[j];
        EXPECT_NEAR(sum, s.b[i], 1e-9);
    }
}
