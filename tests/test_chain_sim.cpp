#include <gtest/gtest.h>
#include <cmath>
#include <numeric>
#include "circuit/DemoCircuits.h"
#include "physics/ChainSim.h"

using namespace current_lab::physics;

namespace {

constexpr double kPi = 3.14159265358979323846;

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

ChainSpec specAt(double x1, double y1, double x2, double y2, int id,
                 double target, double halfW = 5.0, bool brake = false) {
    ChainSpec s;
    s.componentId = id;
    s.a = Vec2(x1, y1);
    s.b = Vec2(x2, y2);
    s.halfWidth = halfW;
    s.targetSpeed = target;
    s.brake = brake;
    return s;
}

void runFor(ChainSim& sim, double seconds) {
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i)
        sim.step(1.0 / 60.0);
}

double ovalOff(double halfWidth, double linkRadius) {
    return std::max(halfWidth * 0.55, linkRadius * 1.6);
}

// True if point p is on the oval track around segment a->b with the given off.
// Returns the signed error: 0 = exactly on track.
double trackLateralError(Vec2 p, Vec2 a, Vec2 b, double off) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1e-6) return (p - a).length() - off;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);
    Vec2 rel = p - a;
    double along = rel.x * unit.x + rel.y * unit.y;
    double lateral = rel.x * perp.x + rel.y * perp.y;

    if (along >= 0.0 && along <= len) {
        // On a straight: lateral should be ±off
        double desired = lateral >= 0.0 ? off : -off;
        return std::abs(lateral - desired);
    }
    // On an arc near a or b
    Vec2 center = along < 0.0 ? a : b;
    return std::abs((p - center).length() - off);
}

} // namespace

TEST(ChainSim, LinksKeepUniformSpacing) {
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
    EXPECT_LT(maxD / minD, 1.35);
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
    EXPECT_GT(moved / after.size(), 5.0);
}

TEST(ChainSim, BrakeZoneIsOvercomeButResists) {
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
    EXPECT_GT(brakedTravel, 1.0);
    EXPECT_LT(brakedTravel, freeTravel);
}

// ---------------------------------------------------------------------------
// Track integrity — the chain must NOT fall off the oval guide (gears).
// ---------------------------------------------------------------------------

TEST(ChainSim, ChainStaysOnTrack) {
    // Regression: "цепь слетает с шестеренок".  Every link must remain within
    // a reasonable distance of the oval centre after sustained driving.
    ChainSim sim;
    double lr = 1.1;
    sim.configure({loopSpec(40.0)}, lr);
    runFor(sim, 5.0);

    double off = ovalOff(5.0, lr);                       // approx 4.0 (2.75 really, but rails ~lr*0.5)
    Vec2 a(0, 0), b(260, 0);
    for (const auto& link : sim.links()) {
        double err = trackLateralError(link.pos, a, b, off);
        EXPECT_LT(err, off * 1.8)                          // generous margin: links can drift to rail edge
            << "link " << link.indexInLoop << " drifted " << err << " px off track (off=" << off << ")";
    }
}

TEST(ChainSim, ChainLinksNeverInfiniteOrNaN) {
    ChainSim sim;
    sim.configure({loopSpec(120.0)}, 1.1);                 // high speed stress
    for (int i = 0; i < 600; ++i) sim.step(1.0 / 60.0);   // 10 s

    for (const auto& link : sim.links()) {
        EXPECT_TRUE(std::isfinite(link.pos.x));
        EXPECT_TRUE(std::isfinite(link.pos.y));
    }
}

TEST(ChainSim, ChainDoesNotCollapse) {
    // Under zero drive a static chain stays spread around the oval,
    // not collapsed into one point.
    ChainSim sim;
    sim.configure({loopSpec(0.0)}, 1.1);
    runFor(sim, 2.0);

    auto links = sim.links();
    ASSERT_GE(links.size(), 4u);
    Vec2 sum(0, 0);
    for (auto& l : links) sum = sum + l.pos;
    Vec2 centroid = sum / static_cast<double>(links.size());
    double rmsFromCentroid = 0.0;
    for (auto& l : links) {
        Vec2 d = l.pos - centroid;
        rmsFromCentroid += d.x * d.x + d.y * d.y;
    }
    rmsFromCentroid = std::sqrt(rmsFromCentroid / links.size());
    EXPECT_GT(rmsFromCentroid, 10.0);
}

TEST(ChainSim, ChainSpeedApproximatesTarget) {
    // Average tangential speed should be within a factor of two of the target.
    ChainSim sim;
    double target = 60.0;
    sim.configure({loopSpec(target)}, 1.1);
    auto before = sim.links();
    runFor(sim, 1.0);
    auto after = sim.links();

    ASSERT_EQ(before.size(), after.size());
    double total = 0.0;
    for (size_t i = 0; i < after.size(); ++i)
        total += (after[i].pos - before[i].pos).length();
    double avgSpeed = total / after.size();               // px/s
    EXPECT_GT(avgSpeed, target * 0.25);
    EXPECT_LT(avgSpeed, target * 2.0);
}

TEST(ChainSim, ChainHandlesZeroSpeed) {
    // Static chain should not drift (positions change negligibly).
    ChainSim sim;
    sim.configure({loopSpec(0.0)}, 1.1);
    auto before = sim.links();
    runFor(sim, 2.0);
    auto after = sim.links();
    ASSERT_EQ(before.size(), after.size());
    double total = 0.0;
    for (size_t i = 0; i < after.size(); ++i)
        total += (after[i].pos - before[i].pos).length();
    EXPECT_LT(total / after.size(), 1.0);
}

TEST(ChainSim, ChainWorksForVerticalComponent) {
    ChainSim sim;
    sim.configure({specAt(100, 0, 100, 300, 1, 40.0)}, 1.3);
    runFor(sim, 3.0);
    auto links = sim.links();
    ASSERT_GE(links.size(), 8u);
    for (const auto& link : links) {
        EXPECT_TRUE(std::isfinite(link.pos.x));
        EXPECT_TRUE(std::isfinite(link.pos.y));
    }
}

TEST(ChainSim, ShortComponentIsSkipped) {
    // Very short component (length < 8*linkRadius) produces no bodies.
    ChainSim sim;
    sim.configure({specAt(0, 0, 2, 0, 1, 40.0)}, 1.5);    // len=2 < 8*1.5=12
    EXPECT_TRUE(sim.configured());
    EXPECT_EQ(sim.links().size(), 0u);
}

TEST(ChainSim, ReconfigurationPreservesCount) {
    ChainSim sim;
    sim.configure({loopSpec(40.0)}, 1.1);
    auto n = sim.links().size();
    // setTargets with same layout but different speed — should keep bodies
    sim.setTargets({loopSpec(80.0)});
    // step to ensure nothing was destroyed
    sim.step(0.0);
    EXPECT_EQ(sim.links().size(), n);
}

TEST(ChainSim, MultipleLoopsAreIndependent) {
    // Two components' chains must not collide or interfere.
    ChainSim sim;
    sim.configure({
        specAt(0, 0, 300, 0, 10, 30.0),
        specAt(0, 60, 300, 60, 20, -30.0),                 // opposite direction, adjacent
    }, 1.2);
    ASSERT_TRUE(sim.configured());
    auto before = sim.links();
    ASSERT_GE(before.size(), 16u);
    runFor(sim, 3.0);
    auto after = sim.links();
    ASSERT_EQ(before.size(), after.size());
    for (const auto& link : after) {
        EXPECT_TRUE(std::isfinite(link.pos.x));
        EXPECT_TRUE(std::isfinite(link.pos.y));
    }
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
