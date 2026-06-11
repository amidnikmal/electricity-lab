#include <gtest/gtest.h>
#include <cmath>
#include "circuit/DemoCircuits.h"
#include "physics/ChainGeometry.h"
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

struct ChainOvalProbe {
    Vec2 a;
    Vec2 b;
    Vec2 unit;
    Vec2 perp;
    double len = 0.0;
    double off = 0.0;

    explicit ChainOvalProbe(const ChainSpec& spec, double linkRadius)
        : a(spec.a),
          b(spec.b),
          unit((spec.b - spec.a).normalized()),
          perp(Vec2(-unit.y, unit.x)),
          len((spec.b - spec.a).length()),
          off(chain_geometry::sprocketPitchRadius(spec.halfWidth, linkRadius)) {}

    double perimeter() const { return 2.0 * len + 2.0 * 3.14159265358979323846 * off; }

    Vec2 pointAt(double t) const {
        double p = perimeter();
        t = std::fmod(t, p);
        if (t < 0.0) t += p;
        double arc = 3.14159265358979323846 * off;

        if (t < len)
            return a + unit * t + perp * off;
        if (t < len + arc) {
            double phi = (t - len) / off;
            double angle = 3.14159265358979323846 * 0.5 - phi;
            return b + perp * (off * std::sin(angle)) + unit * (off * std::cos(angle));
        }
        if (t < 2.0 * len + arc) {
            double s = t - len - arc;
            return b - unit * s - perp * off;
        }

        double phi = (t - 2.0 * len - arc) / off;
        double angle = -3.14159265358979323846 * 0.5 - phi;
        return a + perp * (off * std::sin(angle)) + unit * (off * std::cos(angle));
    }

    double phaseOf(Vec2 pos) const {
        double bestT = 0.0;
        double bestD2 = 1e300;
        constexpr int kSamples = 720;
        for (int i = 0; i < kSamples; ++i) {
            double t = perimeter() * i / kSamples;
            Vec2 d = pointAt(t) - pos;
            double d2 = d.x * d.x + d.y * d.y;
            if (d2 < bestD2) {
                bestD2 = d2;
                bestT = t;
            }
        }
        return bestT / perimeter();
    }
};

const ChainLink* markedLink(const std::vector<ChainLink>& links, int componentId, int index) {
    for (const auto& link : links)
        if (link.componentId == componentId && link.indexInLoop == index)
            return &link;
    return nullptr;
}

double signedPhaseDelta(double before, double after) {
    double delta = after - before;
    if (delta > 0.5) delta -= 1.0;
    if (delta < -0.5) delta += 1.0;
    return delta;
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

TEST(ChainSim, PhaseProbeSeesMarkedLinkAdvance) {
    double radius = 1.1;
    ChainSpec spec = loopSpec(40.0);
    ChainSim sim;
    sim.configure({spec}, radius);
    ChainOvalProbe probe(spec, radius);

    auto beforeLinks = sim.links();
    const ChainLink* before = markedLink(beforeLinks, spec.componentId, 0);
    ASSERT_NE(before, nullptr);
    double beforePhase = probe.phaseOf(before->pos);

    runFor(sim, 2.0);

    auto afterLinks = sim.links();
    const ChainLink* after = markedLink(afterLinks, spec.componentId, 0);
    ASSERT_NE(after, nullptr);
    double afterPhase = probe.phaseOf(after->pos);

    EXPECT_GT(std::abs(signedPhaseDelta(beforePhase, afterPhase)), 0.01);
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
