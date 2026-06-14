#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "circuit/Circuit.h"
#include "physics/ChainGeometry.h"
#include "physics/ChainSim.h"
#include "projection/MechanicsCoupling.h"
#include "projection/MechanicsMapping.h"
#include "solver/CircuitSolver.h"

// Rigid-axle coupling: a node is one physical spindle, so every chain on it must
// turn the SAME way. Before this, each component span its own oval from its
// arbitrary nodeA->nodeB order, so a leg wired "backwards" relative to the
// current made adjacent gears fight (user: "цепи вращаются в разные стороны").
namespace {

namespace cg = current_lab::physics::chain_geometry;
using current_lab::mechanics::AxleCoupling;
using current_lab::mechanics::computeAxleCoupling;
using current_lab::physics::ChainLink;
using current_lab::physics::ChainSim;
using current_lab::physics::ChainSpec;

// A loop n0 -> n1 -> n2 -> n0 driven by a source, with the return leg R2 wired
// nodeA=n0,nodeB=n2 — i.e. BACKWARDS relative to the physical current (n2->n0).
// So R2's branch current is negative while S and R1 are positive: the exact
// shape that used to flip one gear.
struct ReversedLegCircuit {
    Circuit c;
    int n0, n1, n2;
    int s, r1, r2;
    CircuitSolution sol;

    explicit ReversedLegCircuit(double driveCurrent = 0.5) {
        n0 = c.addNode(Vec2(0, 0));
        n1 = c.addNode(Vec2(200, 0));
        n2 = c.addNode(Vec2(200, 150));
        s = c.addComponent(ComponentType::VoltageSource, n0, n1, 5.0);
        r1 = c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
        r2 = c.addComponent(ComponentType::Resistor, n0, n2, 100.0); // reversed leg
        // Physical loop n0->n1->n2->n0: S and R1 with the loop, R2 against its A->B.
        sol.branches.push_back({s, driveCurrent, 5.0, 0.0});
        sol.branches.push_back({r1, driveCurrent, 0.0, 0.0});
        sol.branches.push_back({r2, -driveCurrent, 0.0, 0.0});
    }
};

double currentOf(const CircuitSolution& sol, int id) {
    for (const auto& br : sol.branches)
        if (br.componentId == id) return br.current;
    return 0.0;
}

} // namespace

TEST(MechanicsCoupling, AllChainsInOneMechanismShareOneSign) {
    ReversedLegCircuit rc;
    AxleCoupling cp = computeAxleCoupling(rc.c, &rc.sol);

    // Raw per-component current signs DISAGREE (this is the bug condition).
    EXPECT_GT(currentOf(rc.sol, rc.r1), 0.0);
    EXPECT_LT(currentOf(rc.sol, rc.r2), 0.0);

    // The coupling collapses them onto ONE rotation sign for the whole loop.
    EXPECT_EQ(cp.signFor(rc.s), cp.signFor(rc.r1));
    EXPECT_EQ(cp.signFor(rc.r1), cp.signFor(rc.r2));

    // Every node on the spindle network shares that sense too.
    EXPECT_EQ(cp.nodeSignFor(rc.n0), cp.nodeSignFor(rc.n1));
    EXPECT_EQ(cp.nodeSignFor(rc.n1), cp.nodeSignFor(rc.n2));
    EXPECT_EQ(cp.nodeSignFor(rc.n0), cp.signFor(rc.s));
}

TEST(MechanicsCoupling, SignFollowsTheDominantDrive) {
    ReversedLegCircuit fwd(+0.5);
    ReversedLegCircuit rev(-0.5); // battery reversed -> source current flips
    AxleCoupling a = computeAxleCoupling(fwd.c, &fwd.sol);
    AxleCoupling b = computeAxleCoupling(rev.c, &rev.sol);

    // Reversing the source reverses the WHOLE mechanism, coherently.
    EXPECT_EQ(a.signFor(fwd.s), -b.signFor(rev.s));
    EXPECT_EQ(a.signFor(fwd.r1), -b.signFor(rev.r1));
    EXPECT_EQ(a.signFor(fwd.r2), -b.signFor(rev.r2));
}

TEST(MechanicsCoupling, OpenSwitchAndCapacitorCarryNoAxle) {
    Circuit c;
    int n0 = c.addNode(Vec2(0, 0));
    int n1 = c.addNode(Vec2(100, 0));
    int cap = c.addComponent(ComponentType::Capacitor, n0, n1, 1.0);
    int sw = c.addComponent(ComponentType::Switch, n0, n1, 0.0); // open
    CircuitSolution sol;
    sol.branches.push_back({cap, 0.0, 0.0, 0.0});
    sol.branches.push_back({sw, 0.0, 0.0, 0.0});

    AxleCoupling cp = computeAxleCoupling(c, &sol);
    EXPECT_TRUE(cp.componentSign.find(cap) == cp.componentSign.end());
    EXPECT_TRUE(cp.componentSign.find(sw) == cp.componentSign.end());
}

// The observable payoff: feed ChainSim exactly like MainWindow does (speed =
// couplingSign * |mappedCurrent|) and confirm the two chains meeting at the
// shared node n2 circulate it in the SAME angular direction.
TEST(MechanicsCoupling, ChainsAtSharedNodeRotateTheSameWay) {
    ReversedLegCircuit rc;
    AxleCoupling cp = computeAxleCoupling(rc.c, &rc.sol);

    const double wt = 10.0;
    const double rollerR = cg::linkRadius(wt);
    const double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(wt), rollerR);
    const Vec2 N(200, 150); // n2 — shared by R1 and R2

    auto specFor = [&](int compId, Vec2 a, Vec2 b, int sign) {
        ChainSpec s;
        s.componentId = compId;
        s.a = a;
        s.b = b;
        s.halfWidth = cg::chainHalfWidth(wt);
        double mapped = current_lab::mechanics::chainSpeedFromCurrent(
                            currentOf(rc.sol, compId)) *
                        current_lab::mechanics::kVisualChainSpeed * 100.0;
        s.targetSpeed = std::clamp(sign * std::abs(mapped), -120.0, 120.0);
        return s;
    };

    // Mean signed angular velocity around N of one component's links that ride
    // the arc around N (the links actually wrapping the shared sprocket).
    auto arcAngVel = [&](ChainSim& sim, int compId) {
        std::unordered_map<int, double> before;
        for (const auto& l : sim.links()) {
            if (l.componentId != compId) continue;
            if ((l.pos - N).length() > pitchR * 1.6) continue;
            before[l.indexInLoop] = std::atan2(l.pos.y - N.y, l.pos.x - N.x);
        }
        sim.step(1.0 / 120.0);
        double sum = 0.0;
        int n = 0;
        for (const auto& l : sim.links()) {
            if (l.componentId != compId) continue;
            auto it = before.find(l.indexInLoop);
            if (it == before.end()) continue;
            double after = std::atan2(l.pos.y - N.y, l.pos.x - N.x);
            double d = after - it->second;
            while (d > cg::kPi) d -= 2.0 * cg::kPi;
            while (d < -cg::kPi) d += 2.0 * cg::kPi;
            sum += d;
            ++n;
        }
        return n > 0 ? sum / n : 0.0;
    };

    auto buildSim = [&](int sign1, int sign2) {
        std::vector<ChainSpec> specs = {
            specFor(rc.r1, Vec2(200, 0), Vec2(200, 150), sign1),
            specFor(rc.r2, Vec2(0, 0), Vec2(200, 150), sign2),
        };
        return specs;
    };

    // With the coupling sign both chains turn the shared node the same way.
    {
        ChainSim sim;
        sim.configure(buildSim(cp.signFor(rc.r1), cp.signFor(rc.r2)), rollerR);
        for (int i = 0; i < 30; ++i) sim.step(1.0 / 120.0);
        double w1 = arcAngVel(sim, rc.r1);
        double w2 = arcAngVel(sim, rc.r2);
        ASSERT_NE(w1, 0.0);
        ASSERT_NE(w2, 0.0);
        EXPECT_GT(w1 * w2, 0.0) << "coupled chains still fight at the shared node";
    }

    // Control: the OLD raw per-component signs (current sign) drive them apart —
    // this is the bug the coupling removes.
    {
        int raw1 = currentOf(rc.sol, rc.r1) >= 0 ? 1 : -1;
        int raw2 = currentOf(rc.sol, rc.r2) >= 0 ? 1 : -1;
        ChainSim sim;
        sim.configure(buildSim(raw1, raw2), rollerR);
        for (int i = 0; i < 30; ++i) sim.step(1.0 / 120.0);
        double w1 = arcAngVel(sim, rc.r1);
        double w2 = arcAngVel(sim, rc.r2);
        EXPECT_LT(w1 * w2, 0.0)
            << "control should reproduce the opposite-direction bug";
    }
}
