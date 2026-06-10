#include <gtest/gtest.h>
#include <cmath>
#include "physics/ParticleSim.h"

using namespace current_lab::physics;

namespace {

ChannelSpec straightChannel(double targetSpeed, bool scatterers = false, bool paddle = false) {
    ChannelSpec spec;
    spec.componentId = 1;
    spec.a = Vec2(0, 0);
    spec.b = Vec2(300, 0);
    spec.halfWidth = 6.0;
    spec.targetSpeed = targetSpeed;
    spec.scatterers = scatterers;
    spec.paddle = paddle;
    spec.paddleSpeed = paddle ? 8.0 : 0.0;
    return spec;
}

void runFor(ParticleSim& sim, double seconds) {
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i)
        sim.step(1.0 / 60.0);
}

double meanAxialSpeed(const ParticleSim& sim) {
    auto particles = sim.particles();
    if (particles.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& p : particles) sum += p.vel.x; // axis = +x
    return sum / particles.size();
}

} // namespace

TEST(ParticleSim, MeanDriftConvergesToSolverTarget) {
    ParticleSim sim;
    sim.configure({straightChannel(40.0)}, 1.2);
    runFor(sim, 3.0);
    double mean = meanAxialSpeed(sim);
    EXPECT_GT(mean, 40.0 * 0.6);
    EXPECT_LT(mean, 40.0 * 1.4);
}

TEST(ParticleSim, DriftSignFollowsCurrentSign) {
    ParticleSim sim;
    sim.configure({straightChannel(-35.0)}, 1.2);
    runFor(sim, 3.0);
    EXPECT_LT(meanAxialSpeed(sim), -35.0 * 0.5);
}

TEST(ParticleSim, ParticlesStayInsideTheChannel) {
    ParticleSim sim;
    sim.configure({straightChannel(60.0, /*scatterers=*/true)}, 1.2);
    runFor(sim, 3.0);
    for (const auto& p : sim.particles()) {
        EXPECT_GE(p.pos.x, -2.0);
        EXPECT_LE(p.pos.x, 302.0);
        EXPECT_LE(std::abs(p.pos.y), 6.5);
        EXPECT_TRUE(std::isfinite(p.pos.x));
        EXPECT_TRUE(std::isfinite(p.vel.x));
    }
}

TEST(ParticleSim, ParticlesDoNotInterpenetrate) {
    ParticleSim sim;
    double radius = 1.4;
    sim.configure({straightChannel(30.0)}, radius);
    runFor(sim, 2.0);
    auto particles = sim.particles();
    ASSERT_GE(particles.size(), 4u);
    int deepOverlaps = 0;
    for (size_t i = 0; i < particles.size(); ++i)
        for (size_t j = i + 1; j < particles.size(); ++j) {
            double d = (particles[i].pos - particles[j].pos).length();
            if (d < radius * 1.0) ++deepOverlaps; // less than one radius apart
        }
    EXPECT_EQ(deepOverlaps, 0); // Box2D keeps elastic bodies separated
}

TEST(ParticleSim, PaddleDoesWorkOnParticles) {
    // With zero drive the spinning blades must still physically smack the
    // particles that start inside their sweep (then the area clears out,
    // which is the correct steady state for zero net flow).
    ParticleSim sim;
    sim.configure({straightChannel(0.0, false, /*paddle=*/true)}, 1.2);
    double peakSpeed = 0.0;
    for (int frame = 0; frame < 90; ++frame) { // first 1.5 s
        sim.step(1.0 / 60.0);
        for (const auto& p : sim.particles())
            peakSpeed = std::max(peakSpeed, p.vel.length());
    }
    EXPECT_GT(peakSpeed, 2.0);
}

TEST(ParticleSim, TargetsCanBeUpdatedWithoutRebuild) {
    ParticleSim sim;
    auto spec = straightChannel(30.0);
    sim.configure({spec}, 1.2);
    runFor(sim, 2.0);
    EXPECT_GT(meanAxialSpeed(sim), 10.0);

    spec.targetSpeed = -30.0;
    sim.setTargets({spec});
    runFor(sim, 3.0);
    EXPECT_LT(meanAxialSpeed(sim), -10.0); // reversed without reconfigure
}

TEST(ParticleSim, ScattererChannelStaysPassable) {
    // Regression: the Drude lattice must never seal the resistor; the mean
    // drift through a scatterer channel keeps following the target.
    ParticleSim sim;
    sim.configure({straightChannel(30.0, /*scatterers=*/true)}, 1.28);
    runFor(sim, 4.0);
    double mean = meanAxialSpeed(sim);
    EXPECT_GT(mean, 30.0 * 0.35); // squeezing through, not frozen
}

TEST(ParticleSim, ChannelsCrossingAtAJunctionDoNotJam) {
    // Regression: walls of one channel must not block another channel that
    // meets it at a node (electrons piling up in the corner, frozen).
    ChannelSpec vertical;
    vertical.componentId = 1;
    vertical.a = Vec2(0, 150);
    vertical.b = Vec2(0, 0);
    vertical.halfWidth = 6.0;
    vertical.targetSpeed = 40.0;

    ChannelSpec horizontal;
    horizontal.componentId = 2;
    horizontal.a = Vec2(0, 0); // shares the corner node
    horizontal.b = Vec2(250, 0);
    horizontal.halfWidth = 6.0;
    horizontal.targetSpeed = 40.0;

    ParticleSim sim;
    sim.configure({vertical, horizontal}, 1.2);
    runFor(sim, 4.0);

    double verticalMean = 0.0;
    int verticalCount = 0;
    for (const auto& p : sim.particles()) {
        if (p.componentId == 1) {
            // vertical axis points down (a->b = -y)
            verticalMean += -p.vel.y;
            ++verticalCount;
        }
    }
    ASSERT_GT(verticalCount, 0);
    verticalMean /= verticalCount;
    EXPECT_GT(verticalMean, 40.0 * 0.5); // keeps flowing through the corner
}

TEST(ParticleSim, ParticlesTransferThroughNodesIntoTheNextPipe) {
    // Two pipes in series sharing node 5: the second starts EMPTY, so any
    // particle inside it must have physically arrived through the node buffer.
    ChannelSpec first;
    first.componentId = 1;
    first.a = Vec2(0, 0);
    first.b = Vec2(200, 0);
    first.nodeA = 4;
    first.nodeB = 5;
    first.halfWidth = 6.0;
    first.targetSpeed = 50.0;

    ChannelSpec second = first;
    second.componentId = 2;
    second.a = Vec2(200, 0);
    second.b = Vec2(200, 200); // turns the corner
    second.nodeA = 5;
    second.nodeB = 6;
    second.seedParticles = 0; // starts empty

    ParticleSim sim;
    sim.configure({first, second}, 1.2);
    runFor(sim, 4.0);

    int inSecond = 0;
    int total = 0;
    for (const auto& p : sim.particles()) {
        ++total;
        if (p.componentId == 2) ++inSecond;
    }
    EXPECT_GT(inSecond, 0);   // flow continues into the next conductor
    EXPECT_GT(total, 0);      // nothing leaked out of the world
}

TEST(ParticleSim, LayoutSignatureDetectsGeometryChanges) {
    auto a = straightChannel(10.0);
    auto b = a;
    EXPECT_EQ(ParticleSim::layoutSignature({a}), ParticleSim::layoutSignature({b}));
    b.b = Vec2(400, 0);
    EXPECT_NE(ParticleSim::layoutSignature({a}), ParticleSim::layoutSignature({b}));
    b = a;
    b.targetSpeed = 99.0; // speeds are NOT part of the layout
    EXPECT_EQ(ParticleSim::layoutSignature({a}), ParticleSim::layoutSignature({b}));
}
