#include <gtest/gtest.h>
#include <cmath>
#include "circuit/DemoCircuits.h"
#include "physics/ChainSim.h"

using namespace current_lab::physics;

namespace {

ChainSpec loopSpec(double target, bool brake = false) {
    ChainSpec spec;
    spec.componentId = 1;
    spec.a = Vec2(0, 0);
    spec.b = Vec2(260, 0);
    spec.halfWidth = 5.0;
    spec.targetSpeed = target;
    spec.brake = brake;
    return spec;
}

void runFor(ChainSim& sim, double seconds) {
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i)
        sim.step(1.0 / 60.0);
}

} // namespace

TEST(ChainSim, LinksKeepUniformSpacing) {
    // The joints make the chain inextensible: neighbour spacing stays uniform
    // (regression: "шаг у звеньев разный").
    ChainSim sim;
    sim.configure({loopSpec(40.0)}, 1.1);
    runFor(sim, 3.0);

    auto links = sim.links();
    ASSERT_GE(links.size(), 8u);
    double minD = 1e9, maxD = 0.0;
    for (size_t i = 0; i < links.size(); ++i) {
        const auto& a = links[i];
        const auto& b = links[(i + 1) % links.size()];
        if (a.componentId != b.componentId) continue;
        if ((a.indexInLoop + 1) % a.loopSize != b.indexInLoop) continue;
        double d = (a.pos - b.pos).length();
        minD = std::min(minD, d);
        maxD = std::max(maxD, d);
    }
    ASSERT_LT(minD, 1e8);
    EXPECT_LT(maxD / minD, 1.35); // uniform pitch, enforced by physics
}

TEST(ChainSim, LoopMovesWithTheTargetAndStaysFinite) {
    ChainSim sim;
    sim.configure({loopSpec(40.0)}, 1.1);
    auto before = sim.links();
    runFor(sim, 2.0);
    auto after = sim.links();
    ASSERT_EQ(before.size(), after.size());

    double moved = 0.0;
    for (size_t i = 0; i < after.size(); ++i) {
        EXPECT_TRUE(std::isfinite(after[i].pos.x));
        EXPECT_TRUE(std::isfinite(after[i].pos.y));
        moved += (after[i].pos - before[i].pos).length();
    }
    EXPECT_GT(moved / after.size(), 5.0); // the loop is really running
}

TEST(ChainSim, BrakeZoneIsOvercomeButResists) {
    // Same drive, with and without the friction brake: the braked loop still
    // moves (drive wins) but covers less ground (dissipation is real).
    ChainSim free;
    free.configure({loopSpec(40.0, false)}, 1.1);
    ChainSim braked;
    braked.configure({loopSpec(40.0, true)}, 1.1);

    auto freeBefore = free.links();
    auto brakedBefore = braked.links();
    runFor(free, 2.0);
    runFor(braked, 2.0);
    auto freeAfter = free.links();
    auto brakedAfter = braked.links();

    auto travel = [](const std::vector<ChainLink>& a, const std::vector<ChainLink>& b) {
        double sum = 0.0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            sum += (a[i].pos - b[i].pos).length();
        return sum / std::max<size_t>(1, a.size());
    };
    double freeTravel = travel(freeBefore, freeAfter);
    double brakedTravel = travel(brakedBefore, brakedAfter);
    EXPECT_GT(brakedTravel, 1.0);          // still squeezes through the brake
    EXPECT_LT(brakedTravel, freeTravel);   // but friction visibly costs motion
}

// --- rectangular autolayout -----------------------------------------------------

TEST(DemoLayout, AllPresetComponentsAreAxisAligned) {
    using namespace current_lab::demos;
    for (int d = 0; d < static_cast<int>(DemoCircuit::Count); ++d) {
        Circuit c = buildDemo(static_cast<DemoCircuit>(d));
        for (const auto& comp : c.components) {
            if (comp.type == ComponentType::Ground) continue;
            const Node* a = c.findNode(comp.nodeA);
            const Node* b = c.findNode(comp.nodeB);
            ASSERT_NE(a, nullptr);
            ASSERT_NE(b, nullptr);
            bool axisAligned = std::abs(a->position.x - b->position.x) < 0.5 ||
                               std::abs(a->position.y - b->position.y) < 0.5;
            EXPECT_TRUE(axisAligned)
                << demoName(static_cast<DemoCircuit>(d)) << " component " << comp.id;
        }
    }
}
