#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include "circuit/Circuit.h"
#include "physics/ChannelSpecs.h"
#include "physics/DriftModel.h"
#include "physics/ParticleSim.h"
#include "solver/CircuitSolver.h"

// Water-network mode: pipes joined through junction chambers, dense packing,
// pump-driven flow. The direction contract: in EVERY pipe the mean water
// velocity along the pipe axis (a->b == nodeA->nodeB) has the SIGN of the
// solver branch current — i.e. the flow circulates consistently around the
// loop (top pipe moves right, bottom pipe moves left IN WORLD COORDS — that
// is one circulation, not a contradiction).
namespace {

using namespace current_lab::physics;

// Rectangle loop: source on the left, resistor on top, wires right + bottom.
// Same shape as MainWindow::setupTestCircuit.
Circuit makeLoop(int& srcId, int& resId, int& wire1Id, int& wire2Id,
                 double resistance = 1000.0) {
    Circuit c;
    int gnd = c.addNode(Vec2(200, 300));
    int n1 = c.addNode(Vec2(200, 150));
    int n2 = c.addNode(Vec2(450, 150));
    int corner = c.addNode(Vec2(450, 300));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    srcId = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resId = c.addComponent(ComponentType::Resistor, n1, n2, resistance);
    wire1Id = c.addComponent(ComponentType::Wire, n2, corner, 0.0);
    wire2Id = c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
    return c;
}

void runFor(ParticleSim& sim, double seconds) {
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i)
        sim.step(1.0 / 60.0);
}

struct FlowMeter {
    int componentId = -1;
    Vec2 a;
    Vec2 unit;
    double station = 0.0;
    double particleArea = 0.0;
    std::unordered_map<uint64_t, double> previousT;
    int signedCrossings = 0;

    FlowMeter(const ChannelSpec& spec, double stationFrac, double particleRadius)
        : componentId(spec.componentId),
          a(spec.a),
          unit((spec.b - spec.a).normalized()),
          station((spec.b - spec.a).length() * stationFrac),
          particleArea(3.14159265358979323846 * particleRadius * particleRadius) {}

    void observe(const std::vector<SimParticle>& particles) {
        for (const auto& p : particles) {
            if (p.componentId != componentId) continue;
            Vec2 rel = p.pos - a;
            double t = rel.x * unit.x + rel.y * unit.y;
            auto [it, inserted] = previousT.emplace(p.id, t);
            if (inserted) continue;
            double oldT = it->second;
            if (oldT < station && t >= station)
                ++signedCrossings;
            else if (oldT > station && t <= station)
                --signedCrossings;
            it->second = t;
        }
    }

    double signedVolumePerSecond(double seconds) const {
        return signedCrossings * particleArea / std::max(seconds, 1e-9);
    }
};

double measureFlow(ParticleSim& sim, const ChannelSpec& spec, double stationFrac,
                   double particleRadius, double seconds) {
    FlowMeter meter(spec, stationFrac, particleRadius);
    meter.observe(sim.particles());
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i) {
        sim.step(1.0 / 60.0);
        meter.observe(sim.particles());
    }
    return meter.signedVolumePerSecond(seconds);
}

std::map<int, double> measureFlows(ParticleSim& sim,
                                   const std::vector<ChannelSpec>& specs,
                                   const std::vector<int>& componentIds,
                                   double stationFrac,
                                   double particleRadius,
                                   double seconds) {
    std::vector<FlowMeter> meters;
    for (int id : componentIds) {
        auto it = std::find_if(specs.begin(), specs.end(),
                               [id](const ChannelSpec& s) { return s.componentId == id; });
        if (it != specs.end())
            meters.emplace_back(*it, stationFrac, particleRadius);
    }
    auto observeAll = [&]() {
        auto particles = sim.particles();
        for (auto& meter : meters)
            meter.observe(particles);
    };
    observeAll();
    int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i) {
        sim.step(1.0 / 60.0);
        observeAll();
    }
    std::map<int, double> out;
    for (const auto& meter : meters)
        out[meter.componentId] = meter.signedVolumePerSecond(seconds);
    return out;
}

int countParticlesInAxialBand(const ParticleSim& sim, const ChannelSpec& spec,
                              double t0Frac, double t1Frac) {
    Vec2 unit = (spec.b - spec.a).normalized();
    double length = (spec.b - spec.a).length();
    double lo = length * t0Frac;
    double hi = length * t1Frac;
    int count = 0;
    for (const auto& p : sim.particles()) {
        if (p.componentId != spec.componentId) continue;
        Vec2 rel = p.pos - spec.a;
        double t = rel.x * unit.x + rel.y * unit.y;
        if (t >= lo && t <= hi)
            ++count;
    }
    return count;
}

// Mean velocity along each channel's own axis, keyed by componentId.
std::map<int, double> meanAxisSpeeds(const ParticleSim& sim,
                                     const std::vector<ChannelSpec>& specs) {
    std::map<int, Vec2> axes;
    for (const auto& spec : specs)
        axes[spec.componentId] = (spec.b - spec.a).normalized();
    std::map<int, double> sums;
    std::map<int, int> counts;
    for (const auto& particle : sim.particles()) {
        auto it = axes.find(particle.componentId);
        if (it == axes.end()) continue;
        sums[particle.componentId] +=
            particle.vel.x * it->second.x + particle.vel.y * it->second.y;
        counts[particle.componentId]++;
    }
    for (auto& [id, sum] : sums)
        sum /= std::max(1, counts[id]);
    return sums;
}

} // namespace

TEST(WaterNetwork, SpecsAreConnectedAndPumpHasPaddle) {
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);
    ASSERT_EQ(specs.size(), 4u);
    for (const auto& spec : specs) {
        EXPECT_TRUE(spec.connected);
        EXPECT_EQ(spec.paddle, spec.componentId == srcId);
    }
}

TEST(WaterNetwork, WaterPacksThePipeDensely) {
    // A single straight connected pipe: packing fraction must look like a
    // filled pipe, not a sparse trickle of markers.
    ChannelSpec spec;
    spec.componentId = 1;
    spec.a = Vec2(0, 0);
    spec.b = Vec2(250, 0);
    spec.nodeA = 0;
    spec.nodeB = 1;
    spec.halfWidth = 4.0;
    spec.connected = true;
    double radius = particleWorldRadius(8.0);

    ParticleSim sim;
    sim.configure({spec}, radius);
    double area = 250.0 * 2.0 * spec.halfWidth;
    double covered = sim.particles().size() * 3.14159265 * radius * radius;
    EXPECT_GE(covered / area, 0.35) << "water must visibly fill the pipe";
}

TEST(WaterNetwork, WaterStartsAcrossTheWholePipeWidth) {
    ChannelSpec spec;
    spec.componentId = 1;
    spec.a = Vec2(0, 0);
    spec.b = Vec2(250, 0);
    spec.nodeA = 0;
    spec.nodeB = 1;
    spec.halfWidth = 4.0;
    spec.connected = true;

    ParticleSim sim;
    sim.configure({spec}, particleWorldRadius(8.0));

    double minY = 1e9;
    double maxY = -1e9;
    for (const auto& p : sim.particles()) {
        minY = std::min(minY, p.pos.y);
        maxY = std::max(maxY, p.pos.y);
    }
    EXPECT_LT(minY, -spec.halfWidth * 0.35);
    EXPECT_GT(maxY, spec.halfWidth * 0.35);
}

TEST(WaterNetwork, FlowMeterCountsSignedVolumeCrossings) {
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2, 100.0);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);
    auto specIt = std::find_if(specs.begin(), specs.end(),
                               [w2](const ChannelSpec& s) { return s.componentId == w2; });
    ASSERT_NE(specIt, specs.end());
    double radius = particleWorldRadius(8.0);

    ParticleSim sim;
    sim.configure(specs, radius);
    runFor(sim, 1.0);

    double q = measureFlow(sim, *specIt, 0.5, radius, 4.0);
    EXPECT_GT(q, 0.0);
}

TEST(WaterNetwork, FlowSignMatchesBranchCurrentInEveryPipe) {
    // THE direction contract (user question 2026-06-11): per-pipe mean water
    // velocity along nodeA->nodeB carries the sign of the branch current, so
    // the whole loop circulates in ONE consistent direction.
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);

    ParticleSim sim;
    sim.configure(specs, particleWorldRadius(8.0));
    runFor(sim, 4.0);

    auto means = meanAxisSpeeds(sim, specs);
    for (const auto& branch : sol.branches) {
        auto it = means.find(branch.componentId);
        if (it == means.end()) continue; // ground has no channel
        if (std::abs(branch.current) < 1e-9) continue;
        EXPECT_GT(it->second * branch.current, 0.0)
            << "pipe of component " << branch.componentId
            << " flows against its branch current (I=" << branch.current
            << ", mean v=" << it->second << ")";
        EXPECT_GT(std::abs(it->second), 0.5)
            << "pipe of component " << branch.componentId << " is stagnant";
    }
}

TEST(WaterNetwork, SeriesVolumeFlowIsConservedThroughResistor) {
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2, 100.0);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);

    double radius = particleWorldRadius(8.0);
    ParticleSim sim;
    sim.configure(specs, radius);
    runFor(sim, 2.0);

    auto q = measureFlows(sim, specs, {resId, w1, w2}, 0.5, radius, 8.0);
    ASSERT_EQ(q.size(), 3u);
    EXPECT_GT(q[resId], 0.0);
    EXPECT_GT(q[w1], 0.0);
    EXPECT_GT(q[w2], 0.0);

    double minQ = std::min({std::abs(q[resId]), std::abs(q[w1]), std::abs(q[w2])});
    double maxQ = std::max({std::abs(q[resId]), std::abs(q[w1]), std::abs(q[w2])});
    EXPECT_GT(minQ, 1.0);
    EXPECT_LT(maxQ / std::max(minQ, 1e-9), 6.0)
        << "series flow should stay comparable through resistor and wires";
}

TEST(WaterNetwork, ResistorDoesNotAccumulateWater) {
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2, 100.0);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);
    auto resIt = std::find_if(specs.begin(), specs.end(),
                              [resId](const ChannelSpec& s) { return s.componentId == resId; });
    ASSERT_NE(resIt, specs.end());

    ParticleSim sim;
    sim.configure(specs, particleWorldRadius(8.0));
    runFor(sim, 5.0);

    std::vector<int> counts;
    for (int window = 0; window < 16; ++window) {
        runFor(sim, 0.5);
        counts.push_back(countParticlesInAxialBand(sim, *resIt, 0.25, 0.75));
    }
    ASSERT_GE(counts.size(), 4u);

    double sum = 0.0;
    for (int n : counts) sum += n;
    double avg = sum / counts.size();
    ASSERT_GT(avg, 1.0);

    int first = counts.front();
    int last = counts.back();
    EXPECT_LT(std::abs(last - first), avg * 0.75)
        << "resistor water count trends instead of staying bounded";
    EXPECT_LT(std::abs(last - avg), avg * 0.75)
        << "resistor ends far from its time average";
}

TEST(WaterNetwork, PumpAloneDrivesTheLoop) {
    // The pump must be the CAUSE of motion: kill every per-channel assist
    // (targetSpeed = 0) and keep only the impeller spinning — the loop still
    // has to circulate in the direction of the current.
    int srcId, resId, w1, w2;
    // Strong current (50 mA): the impeller spins at the clamp speed — the
    // causality question is "does the wheel pump?", not "is 2 rad/s enough".
    Circuit c = makeLoop(srcId, resId, w1, w2, 100.0);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);

    std::map<int, double> branchCurrent;
    for (const auto& branch : sol.branches)
        branchCurrent[branch.componentId] = branch.current;

    for (auto& spec : specs)
        spec.targetSpeed = 0.0; // assist OFF — impeller is the only drive

    ParticleSim sim;
    sim.configure(specs, particleWorldRadius(8.0));

    // Time-averaged transport: instantaneous velocities are dominated by the
    // thermal agitation; the pump signal is the mean over the whole run.
    std::map<int, double> means;
    int samples = 0;
    runFor(sim, 1.0); // spin-up
    for (int s = 0; s < 70; ++s) {
        runFor(sim, 0.1);
        for (auto& [id, v] : meanAxisSpeeds(sim, specs))
            means[id] += v;
        ++samples;
    }
    for (auto& [id, v] : means)
        v /= samples;
    // The pump pipe itself must flow with the current...
    double srcFlow = means[srcId] * branchCurrent[srcId];
    EXPECT_GT(srcFlow, 0.0) << "pump pushes its own pipe backwards";
    // ...and the motion must propagate around the loop through the junctions.
    int moving = 0;
    for (const auto& spec : specs) {
        if (spec.componentId == srcId) continue;
        if (means[spec.componentId] * branchCurrent[spec.componentId] > 0.05)
            ++moving;
    }
    EXPECT_GE(moving, 2) << "pump-driven flow failed to propagate past the junctions";
}

TEST(WaterNetwork, PumpWheelDoesNotPlugThePipe) {
    // Regression «шарики собираются внизу, насос не проталкивает»: the
    // corridor above the impeller blade tips must pass particles freely
    // (>= 1.4 diameters), otherwise the wheel is a plug and the loop water
    // piles up at the pump intake.
    for (double wt : {6.0, 8.0, 12.0}) {
        double hw = wt * 0.5;
        double r = particleWorldRadius(wt);
        // Axle on the wall at -hw; tips reach -hw + R.
        double tipLateral = -hw + pumpImpellerRadius(hw);
        double corridor = hw - tipLateral;
        EXPECT_GE(corridor, 2.8 * r) << "wt=" << wt << ": pump seals the pipe";
    }
}

TEST(WaterNetwork, NoParticleEscapesThePlumbing) {
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);

    double radius = particleWorldRadius(8.0);
    ParticleSim sim;
    sim.configure(specs, radius);
    runFor(sim, 5.0);

    auto insideSomething = [&](Vec2 pos) {
        for (const auto& spec : specs) {
            Vec2 unit = (spec.b - spec.a).normalized();
            Vec2 perp(-unit.y, unit.x);
            Vec2 rel = pos - spec.a;
            double t = rel.x * unit.x + rel.y * unit.y;
            double lat = rel.x * perp.x + rel.y * perp.y;
            double rj = junctionRadius(spec.halfWidth);
            if (t >= -rj && t <= (spec.b - spec.a).length() + rj &&
                std::abs(lat) <= spec.halfWidth + radius * 2.0)
                return true;
            if ((pos - spec.a).length() <= rj + radius * 2.0) return true;
            if ((pos - spec.b).length() <= rj + radius * 2.0) return true;
        }
        return false;
    };

    int escaped = 0;
    for (const auto& particle : sim.particles())
        if (!insideSomething(particle.pos)) ++escaped;
    EXPECT_EQ(escaped, 0);
}

TEST(WaterNetwork, ParticlesActuallyCrossJunctions) {
    // Connectivity is physical: with circulation running, per-pipe particle
    // counts change because water moves from pipe to pipe through chambers.
    int srcId, resId, w1, w2;
    Circuit c = makeLoop(srcId, resId, w1, w2);
    CircuitSolver solver;
    CircuitSolution sol = solver.solve(c);
    auto specs = makeChannelSpecs(c, &sol, 8.0, /*waterWorld=*/true);

    ParticleSim sim;
    sim.configure(specs, particleWorldRadius(8.0));

    auto countByComponent = [&]() {
        std::map<int, int> counts;
        for (const auto& particle : sim.particles())
            counts[particle.componentId]++;
        return counts;
    };
    auto total = [](const std::map<int, int>& m) {
        int sum = 0;
        for (auto& [id, n] : m) sum += n;
        return sum;
    };

    auto before = countByComponent();
    runFor(sim, 4.0);
    auto after = countByComponent();

    EXPECT_EQ(total(before), total(after)) << "water neither created nor destroyed";
    bool anyChanged = false;
    for (auto& [id, n] : after)
        if (n != before[id]) anyChanged = true;
    EXPECT_TRUE(anyChanged) << "no particle ever crossed a junction";
}
