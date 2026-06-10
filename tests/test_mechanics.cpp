#include <gtest/gtest.h>
#include <cmath>
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "projection/ProjectionBuilder.h"
#include "projection/MechanicsMapping.h"
#include "ui/DualViewState.h"

namespace {

using namespace current_lab::mechanics;
using namespace current_lab::projection;

Circuit makeRlcCircuit(int& capId, int& indId, int& resId) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 200));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    int n3 = c.addNode(Vec2(400, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resId = c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
    indId = c.addComponent(ComponentType::Inductor, n2, n3, 1.0);
    capId = c.addComponent(ComponentType::Capacitor, n3, gnd, 1e-3);
    return c;
}

ViewParams spinParams() {
    ViewParams p;
    p.layers.current = true;
    p.layers.potential = true;
    p.layers.heat = true;
    p.layers.magnetic = true;
    return p;
}

} // namespace

// --- mapping: monotonic, sign-correct, power-preserving ---------------------

TEST(MechanicsMapping, ChainSpeedIsMonotonicAndSignCorrect) {
    EXPECT_GT(chainSpeedFromCurrent(0.2), chainSpeedFromCurrent(0.1));
    EXPECT_GT(chainSpeedFromCurrent(0.1), 0.0);
    EXPECT_LT(chainSpeedFromCurrent(-0.1), 0.0);
    EXPECT_DOUBLE_EQ(chainSpeedFromCurrent(-0.1), -chainSpeedFromCurrent(0.1));
    EXPECT_DOUBLE_EQ(chainSpeedFromCurrent(0.0), 0.0);
}

TEST(MechanicsMapping, TensionIsMonotonicAndSignCorrect) {
    EXPECT_GT(tensionFromPotential(5.0), tensionFromPotential(2.0));
    EXPECT_LT(tensionFromPotential(-1.0), 0.0);
    EXPECT_DOUBLE_EQ(tensionFromPotential(0.0), 0.0);
}

TEST(MechanicsMapping, MechanicalPowerEqualsElectricalPower) {
    for (double v : {-3.0, 0.5, 5.0}) {
        for (double i : {-0.2, 0.01, 0.3}) {
            double mech = mechanicalPower(tensionFromPotential(v), chainSpeedFromCurrent(i));
            EXPECT_DOUBLE_EQ(mech, v * i);
        }
    }
}

TEST(MechanicsMapping, SpringCompressionTracksCapVoltage) {
    EXPECT_GT(springCompressionFromVoltage(4.0), springCompressionFromVoltage(2.0));
    EXPECT_LT(springCompressionFromVoltage(-2.0), 0.0);
    EXPECT_DOUBLE_EQ(springCompressionFromVoltage(0.0), 0.0);
}

TEST(MechanicsMapping, FlywheelMomentumTracksInductorCurrent) {
    EXPECT_GT(flywheelAngularMomentumFromCurrent(0.4), flywheelAngularMomentumFromCurrent(0.2));
    EXPECT_LT(flywheelAngularMomentumFromCurrent(-0.2), 0.0);
}

TEST(MechanicsMapping, AnalogEnergiesMatchElectricalEnergies) {
    EXPECT_DOUBLE_EQ(springEnergy(1e-3, 4.0), current_lab::physics::capacitorEnergy(1e-3, 4.0));
    EXPECT_DOUBLE_EQ(flywheelEnergy(2.0, 0.3), current_lab::physics::inductorEnergy(2.0, 0.3));
}

TEST(MechanicsMapping, CrankDynamoEmfIsMonotonicSignedAndClamped) {
    using current_lab::mechanics::emfFromCrankSpeed;
    EXPECT_GT(emfFromCrankSpeed(2.0), emfFromCrankSpeed(1.0));
    EXPECT_LT(emfFromCrankSpeed(-2.0), 0.0);
    EXPECT_DOUBLE_EQ(emfFromCrankSpeed(0.0), 0.0);
    EXPECT_DOUBLE_EQ(emfFromCrankSpeed(100.0), 12.0);   // clamped
    EXPECT_DOUBLE_EQ(emfFromCrankSpeed(-100.0), -12.0); // clamped
}

TEST(MechanicsMapping, BrakeHeatOnlyCountsDissipation) {
    EXPECT_DOUBLE_EQ(brakeHeatFromPower(ComponentType::Resistor, 2.0), 2.0);
    EXPECT_DOUBLE_EQ(brakeHeatFromPower(ComponentType::VoltageSource, -2.0), 0.0);
    EXPECT_DOUBLE_EQ(brakeHeatFromPower(ComponentType::Resistor, -0.5), 0.0);
}

// --- projection: built from the same model + solution ------------------------

TEST(MechanicsProjection, ElementsMatchOtherProjectionsExactly) {
    int capId, indId, resId;
    Circuit c = makeRlcCircuit(capId, indId, resId);
    CircuitSolver solver;
    TransientState state;
    for (int i = 0; i < 500; ++i)
        solver.stepTransient(c, state, 1e-3);
    auto solution = solver.solveTransientSnapshot(c, state);

    ViewParams params = spinParams();
    auto spin = buildProjection(ProjectionKind::Mechanical, c, &solution, params);
    auto physics = buildProjection(ProjectionKind::Physics, c, &solution, params);

    ASSERT_EQ(spin.elements.size(), physics.elements.size());
    for (size_t i = 0; i < spin.elements.size(); ++i) {
        EXPECT_EQ(spin.elements[i].componentId, physics.elements[i].componentId);
        EXPECT_DOUBLE_EQ(spin.elements[i].current, physics.elements[i].current);
        EXPECT_DOUBLE_EQ(spin.elements[i].voltageA, physics.elements[i].voltageA);
        EXPECT_DOUBLE_EQ(spin.elements[i].storedEnergy, physics.elements[i].storedEnergy);
    }
}

TEST(MechanicsProjection, EmitsMechanicalAnalogShapes) {
    int capId, indId, resId;
    Circuit c = makeRlcCircuit(capId, indId, resId);
    CircuitSolver solver;
    auto solution = solver.solve(c);

    auto result = buildProjection(ProjectionKind::Mechanical, c, &solution, spinParams());

    EXPECT_FALSE(result.prims.lines.empty());     // chain rails and links
    EXPECT_FALSE(result.prims.circles.empty());   // crank wheel / flywheel / pulleys
    EXPECT_FALSE(result.prims.polylines.empty()); // capacitor spring zigzag
    EXPECT_FALSE(result.prims.quads.empty());     // brake pads / anchor block

    bool hasBrakeLabel = false;
    for (const auto& label : result.prims.labels)
        hasBrakeLabel = hasBrakeLabel || label.text.find("brake") != std::string::npos;
    EXPECT_TRUE(hasBrakeLabel);
}

TEST(MechanicsProjection, ChainLinkPositionsFollowCurrentSign) {
    // The chain phase at small t>0 moves along +I; with reversed source it
    // must move the other way.
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(300, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    int src = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 100.0);
    int wireId = c.addComponent(ComponentType::Wire, n2, gnd, 0.0);
    (void)wireId;

    CircuitSolver solver;
    auto solution = solver.solve(c);

    ViewParams params = spinParams();
    params.time = 0.001;
    auto forward = buildProjection(ProjectionKind::Mechanical, c, &solution, params);

    Component* source = c.findComponent(src);
    ASSERT_NE(source, nullptr);
    source->value = -5.0;
    auto reversedSolution = solver.solve(c);
    auto reversed = buildProjection(ProjectionKind::Mechanical, c, &reversedSolution, params);

    // Same primitive structure, but the animated link phases differ because
    // the chain speed (sign of I) reversed.
    ASSERT_EQ(forward.prims.lines.size(), reversed.prims.lines.size());
    bool anyDifferent = false;
    for (size_t i = 0; i < forward.prims.lines.size(); ++i) {
        if (std::abs(forward.prims.lines[i].a.x - reversed.prims.lines[i].a.x) > 1e-9 ||
            std::abs(forward.prims.lines[i].a.y - reversed.prims.lines[i].a.y) > 1e-9) {
            anyDifferent = true;
            break;
        }
    }
    EXPECT_TRUE(anyDifferent);
}

TEST(MechanicsProjection, SpringCompressesAsCapacitorCharges) {
    int capId, indId, resId;
    Circuit c = makeRlcCircuit(capId, indId, resId);
    CircuitSolver solver;

    TransientState empty;
    auto uncharged = solver.solveTransientSnapshot(c, empty);
    auto unchargedSpin = buildProjection(ProjectionKind::Mechanical, c, &uncharged, spinParams());

    TransientState charged;
    charged.capVoltage[capId] = 5.0;
    auto chargedSolution = solver.solveTransientSnapshot(c, charged);
    auto chargedSpin = buildProjection(ProjectionKind::Mechanical, c, &chargedSolution, spinParams());

    // Spring polyline (the longest polyline) must contract when charged.
    auto springSpanX = [](const ProjectionResult& r) {
        double best = 0.0;
        for (const auto& poly : r.prims.polylines) {
            if (poly.pts.size() < 5) continue;
            double span = std::abs(poly.pts.back().x - poly.pts.front().x);
            best = std::max(best, span);
        }
        return best;
    };

    double freeSpan = springSpanX(unchargedSpin);
    double chargedSpan = springSpanX(chargedSpin);
    ASSERT_GT(freeSpan, 0.0);
    EXPECT_LT(chargedSpan, freeSpan - 1e-6);
}

// --- camera sync across all three panes --------------------------------------

TEST(TripleView, PanSyncsAllThreeCameras) {
    current_lab::ui::DualViewState state;
    state.syncCameras = true;
    state.pan(current_lab::ui::DualViewPane::Mechanics, Vec2(15, -7));
    EXPECT_TRUE(current_lab::ui::cameraApproximatelyEqual(state.circuitCamera, state.mechanicsCamera));
    EXPECT_TRUE(current_lab::ui::cameraApproximatelyEqual(state.physicsCamera, state.mechanicsCamera));
}

TEST(TripleView, SplitCoversFullWidth) {
    auto split = current_lab::ui::computeTripleViewPaneSplit(1200.0f, 8.0f);
    EXPECT_GT(split.circuitWidth, 100.0f);
    EXPECT_GT(split.physicsWidth, 100.0f);
    EXPECT_GT(split.mechanicsWidth, 100.0f);
    EXPECT_NEAR(split.circuitWidth + split.physicsWidth + split.mechanicsWidth + 16.0f,
                1200.0f, 2.0f);
}
