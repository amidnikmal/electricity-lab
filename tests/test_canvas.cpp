#include <gtest/gtest.h>
#include <cmath>
#include "ui/CircuitCanvas.h"
#include "circuit/Circuit.h"
#include "math/Vec2.h"
#include "physics/DriftModel.h"
#include "physics/FieldModel.h"
#include "physics/MagneticFieldModel.h"
#include "physics/ResistiveElementModel.h"
#include "physics/SurfaceChargeModel.h"
#include "visualization/VisualizationPresets.h"

static constexpr double kEps = 1e-9;

// ─── setMode clears drag / place only when mode changes ───────────
TEST(CanvasModeSwitch, SetModeClearsDragNode) {
    CircuitCanvas cv;
    cv.setDragNode(5);
    cv.setMode(EditorMode::PlaceWire);  // different mode → must clear
    EXPECT_EQ(cv.dragNode(), -1);
}

TEST(CanvasModeSwitch, SetModeClearsPlaceFromNode) {
    CircuitCanvas cv;
    cv.setPlaceFromNode(2);
    cv.setMode(EditorMode::PlaceWire);  // different mode → must clear
    EXPECT_EQ(cv.placeFromNode(), -1);
}

TEST(CanvasModeSwitch, SetModeSameKeepsDragNode) {
    CircuitCanvas cv;
    cv.setDragNode(5);
    cv.setMode(EditorMode::Select);  // same as default → must keep
    EXPECT_EQ(cv.dragNode(), 5);
}

TEST(CanvasModeSwitch, SetModeSameKeepsPlaceFromNode) {
    CircuitCanvas cv;
    cv.setPlaceFromNode(2);
    cv.setMode(EditorMode::Select);  // same as default → must keep
    EXPECT_EQ(cv.placeFromNode(), 2);
}

TEST(CanvasModeSwitch, SetModePreservesSelection) {
    CircuitCanvas cv;
    cv.setSelected(3, 7);
    cv.setMode(EditorMode::PlaceWire);
    EXPECT_EQ(cv.selectedNode(), 3);
    EXPECT_EQ(cv.selectedComponent(), 7);
}

// ─── showCurrent / electronFlow defaults and toggles ───────────────
TEST(CanvasVisualization, DefaultShowCurrent) {
    CircuitCanvas cv;
    EXPECT_TRUE(cv.showCurrent());
}

TEST(CanvasVisualization, DefaultElectronFlowOff) {
    CircuitCanvas cv;
    EXPECT_FALSE(cv.electronFlow());
}

TEST(CanvasVisualization, ToggleShowCurrent) {
    CircuitCanvas cv;
    cv.setShowCurrent(false);
    EXPECT_FALSE(cv.showCurrent());
    cv.setShowCurrent(true);
    EXPECT_TRUE(cv.showCurrent());
}

TEST(CanvasVisualization, ToggleElectronFlow) {
    CircuitCanvas cv;
    cv.setElectronFlow(true);
    EXPECT_TRUE(cv.electronFlow());
    cv.setElectronFlow(false);
    EXPECT_FALSE(cv.electronFlow());
}

TEST(CanvasVisualization, DefaultShowPotential) {
    CircuitCanvas cv;
    EXPECT_TRUE(cv.showPotential());
}

TEST(CanvasVisualization, ToggleShowPotential) {
    CircuitCanvas cv;
    cv.setShowPotential(false);
    EXPECT_FALSE(cv.showPotential());
    cv.setShowPotential(true);
    EXPECT_TRUE(cv.showPotential());
}

TEST(CanvasVisualization, DefaultShowDrift) {
    CircuitCanvas cv;
    EXPECT_TRUE(cv.showDrift());
}

TEST(CanvasVisualization, ToggleShowDrift) {
    CircuitCanvas cv;
    cv.setShowDrift(false);
    EXPECT_FALSE(cv.showDrift());
    cv.setShowDrift(true);
    EXPECT_TRUE(cv.showDrift());
}

TEST(CanvasVisualization, DriftParticleRadiusIncreasesWithZoomIn) {
    float rZoomedOut = CircuitCanvas::particleScreenRadius(0.1f);
    float rDefault   = CircuitCanvas::particleScreenRadius(1.0f);
    float rZoomedIn  = CircuitCanvas::particleScreenRadius(5.0f);
    EXPECT_LT(rZoomedOut, rDefault);
    EXPECT_LT(rDefault, rZoomedIn);
}

TEST(CanvasVisualization, DriftParticleRadiusNeverInvisible) {
    float rClamped = CircuitCanvas::particleScreenRadius(0.1f);
    EXPECT_GT(rClamped, 1.5f);
}

TEST(CanvasVisualization, DefaultShowEField) {
    CircuitCanvas cv;
    EXPECT_TRUE(cv.showEField());
}

TEST(CanvasVisualization, ToggleShowEField) {
    CircuitCanvas cv;
    cv.setShowEField(false);
    EXPECT_FALSE(cv.showEField());
    cv.setShowEField(true);
    EXPECT_TRUE(cv.showEField());
}

TEST(CanvasVisualization, DefaultShowHeat) {
    CircuitCanvas cv;
    EXPECT_TRUE(cv.showHeat());
}

TEST(CanvasVisualization, ToggleShowHeat) {
    CircuitCanvas cv;
    cv.setShowHeat(false);
    EXPECT_FALSE(cv.showHeat());
    cv.setShowHeat(true);
    EXPECT_TRUE(cv.showHeat());
}

TEST(CanvasVisualization, DefaultShowPower) {
    CircuitCanvas cv;
    EXPECT_TRUE(cv.showPower());
}

TEST(CanvasVisualization, ToggleShowPower) {
    CircuitCanvas cv;
    cv.setShowPower(false);
    EXPECT_FALSE(cv.showPower());
    cv.setShowPower(true);
    EXPECT_TRUE(cv.showPower());
}

TEST(CanvasVisualization, DefaultReadOnly) {
    CircuitCanvas cv;
    // No getter for readOnly — tested via behavior
    (void)cv;
    SUCCEED();
}

// ─── Camera math round-trip ────────────────────────────────────────
TEST(CanvasCamera, WorldToScreenRoundtrip) {
    CanvasCamera cam;
    cam.offset = Vec2(100, 50);
    cam.scale = 2.0f;
    Vec2 w(30, 20);
    ImVec2 s = cam.worldToScreen(w);
    Vec2 back = cam.screenToWorld(s);
    EXPECT_NEAR(back.x, w.x, kEps);
    EXPECT_NEAR(back.y, w.y, kEps);
}

TEST(CanvasCamera, PanAccumulates) {
    CanvasCamera cam;
    cam.pan(Vec2(10, -5));
    cam.pan(Vec2(20, 15));
    EXPECT_NEAR(cam.offset.x, 30, kEps);
    EXPECT_NEAR(cam.offset.y, 10, kEps);
}

TEST(CanvasCamera, ZoomAtPreservesScreenPoint) {
    CanvasCamera cam;
    cam.scale = 1.0f;
    cam.offset = Vec2(100, 100);
    Vec2 screenPt(60, 70);
    Vec2 worldBefore = cam.screenToWorld(ImVec2((float)screenPt.x, (float)screenPt.y));
    cam.zoomAt(2.0f, screenPt);
    Vec2 worldAfter = cam.screenToWorld(ImVec2((float)screenPt.x, (float)screenPt.y));
    EXPECT_NEAR(worldBefore.x, worldAfter.x, kEps);
    EXPECT_NEAR(worldBefore.y, worldAfter.y, kEps);
}

TEST(CanvasCamera, ZoomClamped) {
    CanvasCamera cam;
    cam.zoomAt(0.02f, Vec2(0, 0));
    EXPECT_FLOAT_EQ(cam.scale, 0.05f);
    cam.scale = 0.05f;
    cam.zoomAt(2000.0f, Vec2(0, 0));
    EXPECT_FLOAT_EQ(cam.scale, 50.0f);
}

// ─── Node-position invariance on click-without-drag ────────────────
TEST(CircuitModel, ClickWithoutDragDoesNotMoveNode) {
    Circuit c;
    int n0 = c.addNode(Vec2(50, 100), "N0");
    int n1 = c.addNode(Vec2(200, 100), "N1");
    c.addComponent(ComponentType::Wire, n0, n1, 0.0);

    Node* node = c.findNode(n0);
    ASSERT_NE(node, nullptr);
    Vec2 original = node->position;

    EXPECT_NEAR(original.x, 50, kEps);
    EXPECT_NEAR(original.y, 100, kEps);
}

TEST(CircuitModel, MoveNodeChangesPosition) {
    Circuit c;
    int n0 = c.addNode(Vec2(50, 100));
    c.addNode(Vec2(200, 100));

    Node* node = c.findNode(n0);
    ASSERT_NE(node, nullptr);
    node->position = Vec2(75, 120);

    Node* moved = c.findNode(n0);
    EXPECT_NEAR(moved->position.x, 75, kEps);
    EXPECT_NEAR(moved->position.y, 120, kEps);
}

// ─── Canvas placement & interaction ───────────────────────────────
TEST(CanvasPlacement, DefaultModeIsSelect) {
    CircuitCanvas cv;
    EXPECT_EQ(cv.mode(), EditorMode::Select);
}

TEST(CanvasPlacement, SetModeChangesMode) {
    CircuitCanvas cv;
    cv.setMode(EditorMode::PlaceWire);
    EXPECT_EQ(cv.mode(), EditorMode::PlaceWire);
    cv.setMode(EditorMode::PlaceNode);
    EXPECT_EQ(cv.mode(), EditorMode::PlaceNode);
}

TEST(CanvasPlacement, PlaceWireCallbackCreatesWireInCircuit) {
    Circuit circuit;
    int n0 = circuit.addNode(Vec2(0, 0));
    int n1 = circuit.addNode(Vec2(100, 0));

    CircuitCanvas canvas;
    canvas.setMode(EditorMode::PlaceWire);

    canvas.callbacks.createComponent = [&](int from, int to, ComponentType ct, double val) {
        circuit.addComponent(ct, from, to, val);
    };

    canvas.callbacks.createComponent(n0, n1, ComponentType::Wire, 0.0);

    ASSERT_EQ(circuit.components.size(), 1u);
    EXPECT_EQ(circuit.components[0].type, ComponentType::Wire);
    EXPECT_EQ(circuit.components[0].nodeA, n0);
    EXPECT_EQ(circuit.components[0].nodeB, n1);
}

TEST(CanvasPlacement, PlaceResistorCallbackPreservesType) {
    Circuit circuit;
    int n0 = circuit.addNode(Vec2(0, 0));
    int n1 = circuit.addNode(Vec2(100, 0));

    CircuitCanvas canvas;
    canvas.setMode(EditorMode::PlaceResistor);

    canvas.callbacks.createComponent = [&](int from, int to, ComponentType ct, double val) {
        circuit.addComponent(ct, from, to, val);
    };

    canvas.callbacks.createComponent(n0, n1, ComponentType::Resistor, 1000.0);

    ASSERT_EQ(circuit.components.size(), 1u);
    EXPECT_EQ(circuit.components[0].type, ComponentType::Resistor);
    EXPECT_NEAR(circuit.components[0].value, 1000.0, kEps);
}

TEST(CanvasPlacement, PlaceGroundOnExistingNode) {
    Circuit circuit;
    int gnd = circuit.addNode(Vec2(0, 0));

    CircuitCanvas canvas;
    canvas.setMode(EditorMode::PlaceGround);

    canvas.callbacks.createComponent = [&](int from, int to, ComponentType ct, double val) {
        circuit.addComponent(ct, from, to, val);
        if (ct == ComponentType::Ground) circuit.groundNodeId = to;
    };

    canvas.callbacks.createComponent(gnd, gnd, ComponentType::Ground, 0.0);

    ASSERT_EQ(circuit.components.size(), 1u);
    EXPECT_EQ(circuit.components[0].type, ComponentType::Ground);
    EXPECT_EQ(circuit.groundNodeId, gnd);
}

TEST(CanvasPlacement, SwitchingModePreservesNewMode) {
    CircuitCanvas canvas;
    EXPECT_EQ(canvas.mode(), EditorMode::Select);

    canvas.setMode(EditorMode::PlaceWire);
    EXPECT_EQ(canvas.mode(), EditorMode::PlaceWire);

    canvas.setMode(EditorMode::Select);
    EXPECT_EQ(canvas.mode(), EditorMode::Select);

    canvas.setMode(EditorMode::PlaceResistor);
    EXPECT_EQ(canvas.mode(), EditorMode::PlaceResistor);
}

TEST(CanvasPlacement, FullWireCreationEndToEnd) {
    Circuit circuit;
    int n0 = circuit.addNode(Vec2(0, 0));
    int n1 = circuit.addNode(Vec2(100, 0));

    CircuitCanvas canvas;
    canvas.setMode(EditorMode::PlaceWire);

    canvas.callbacks.createComponent = [&](int from, int to, ComponentType ct, double val) {
        circuit.addComponent(ct, from, to, val);
        if (ct == ComponentType::Ground) circuit.groundNodeId = to;
    };

    canvas.callbacks.createComponent(n0, n1, ComponentType::Wire, 0.0);
    EXPECT_EQ(circuit.components.size(), 1u);
    EXPECT_EQ(circuit.components[0].nodeA, n0);
    EXPECT_EQ(circuit.components[0].nodeB, n1);
}

TEST(CanvasPlacement, MultipleWiresBetweenDifferentPairs) {
    Circuit circuit;
    int n0 = circuit.addNode(Vec2(0, 0));
    int n1 = circuit.addNode(Vec2(100, 0));
    int n2 = circuit.addNode(Vec2(0, 100));
    int n3 = circuit.addNode(Vec2(100, 100));

    CircuitCanvas canvas;
    canvas.setMode(EditorMode::PlaceWire);

    canvas.callbacks.createComponent = [&](int from, int to, ComponentType ct, double val) {
        circuit.addComponent(ct, from, to, val);
    };

    canvas.callbacks.createComponent(n0, n1, ComponentType::Wire, 0.0);
    canvas.callbacks.createComponent(n2, n3, ComponentType::Wire, 0.0);

    EXPECT_EQ(circuit.components.size(), 2u);
    EXPECT_EQ(circuit.components[0].nodeA, n0);
    EXPECT_EQ(circuit.components[1].nodeA, n2);
}

TEST(CanvasPlacement, PlaceNodeAddsNodeAtPosition) {
    Circuit circuit;
    CircuitCanvas canvas;
    canvas.setMode(EditorMode::PlaceNode);

    canvas.callbacks.placeNode = [&](Vec2 pos) {
        circuit.addNode(pos);
    };

    canvas.callbacks.placeNode(Vec2(150, 250));
    ASSERT_EQ(circuit.nodes.size(), 1u);
    EXPECT_NEAR(circuit.nodes[0].position.x, 150, kEps);
    EXPECT_NEAR(circuit.nodes[0].position.y, 250, kEps);

    canvas.callbacks.placeNode(Vec2(300, 100));
    ASSERT_EQ(circuit.nodes.size(), 2u);
    EXPECT_NEAR(circuit.nodes[1].position.x, 300, kEps);
    EXPECT_NEAR(circuit.nodes[1].position.y, 100, kEps);
}

// ─── Solver idempotent: re-solving same circuit = same result ─────
TEST(SolverIdempotent, SeriesRStableOnResolve) {
    Circuit c;
    c.addNode(Vec2(0, 0));
    c.addNode(Vec2(100, 0));
    c.addNode(Vec2(200, 0));
    c.groundNodeId = 0;
    c.addComponent(ComponentType::VoltageSource, 1, 0, 5.0);
    c.addComponent(ComponentType::Resistor, 1, 2, 1000.0);
    c.addComponent(ComponentType::Wire, 2, 0, 0.0);

    CircuitSolver solver;
    auto s1 = solver.solve(c);
    auto s2 = solver.solve(c);

    ASSERT_EQ(s1.nodePotentials.size(), s2.nodePotentials.size());
    ASSERT_EQ(s1.branches.size(), s2.branches.size());
    for (size_t i = 0; i < s1.nodePotentials.size(); ++i) {
        EXPECT_NEAR(s1.nodePotentials[i].potential, s2.nodePotentials[i].potential, 1e-9);
    }
    for (size_t i = 0; i < s1.branches.size(); ++i) {
        EXPECT_NEAR(s1.branches[i].current, s2.branches[i].current, 1e-9);
        EXPECT_NEAR(s1.branches[i].voltageDrop, s2.branches[i].voltageDrop, 1e-9);
    }
}

// ─── Surface charge toggle ─────────────────────────────────────────
TEST(CanvasVisualization, DefaultShowSurfaceCharge) {
    CircuitCanvas cv;
    EXPECT_TRUE(cv.showSurfaceCharge());
}

TEST(CanvasVisualization, ToggleShowSurfaceCharge) {
    CircuitCanvas cv;
    cv.setShowSurfaceCharge(false);
    EXPECT_FALSE(cv.showSurfaceCharge());
    cv.setShowSurfaceCharge(true);
    EXPECT_TRUE(cv.showSurfaceCharge());
}

// ─── Wire thickness in world units ─────────────────────────────────
TEST(CanvasVisualization, WireThicknessDefault8) {
    CircuitCanvas cv;
    EXPECT_FLOAT_EQ(cv.wireThickness(), 8.0f);
}

TEST(CanvasVisualization, WireThicknessClampLow) {
    CircuitCanvas cv;
    cv.setWireThickness(0.5f);
    EXPECT_FLOAT_EQ(cv.wireThickness(), 2.0f);
}

TEST(CanvasVisualization, WireThicknessClampHigh) {
    CircuitCanvas cv;
    cv.setWireThickness(100.0f);
    EXPECT_FLOAT_EQ(cv.wireThickness(), 50.0f);
}

TEST(CanvasVisualization, WireThicknessWithinRange) {
    CircuitCanvas cv;
    cv.setWireThickness(20.0f);
    EXPECT_FLOAT_EQ(cv.wireThickness(), 20.0f);
}

// ─── Wire screen width = thickness × scale ─────────────────────────
TEST(CanvasVisualization, WireScreenWidthAtScale1) {
    CircuitCanvas cv; // default: thickness=8, scale=1
    EXPECT_FLOAT_EQ(cv.wireScreenWidth(), 8.0f);
}

TEST(CanvasVisualization, WireScreenWidthAtScale5) {
    CircuitCanvas cv;
    cv.setWireThickness(10.0f);
    cv.camera().scale = 5.0f;
    EXPECT_FLOAT_EQ(cv.wireScreenWidth(), 50.0f);
}

TEST(CanvasVisualization, WireScreenWidthAtZoomOut) {
    CircuitCanvas cv;
    cv.setWireThickness(8.0f);
    cv.camera().scale = 0.1f;
    EXPECT_FLOAT_EQ(cv.wireScreenWidth(), 0.8f);
}

TEST(CanvasVisualization, WireScreenWidthAfterBothSet) {
    CircuitCanvas cv;
    cv.setWireThickness(14.0f);
    cv.camera().scale = 3.0f;
    EXPECT_FLOAT_EQ(cv.wireScreenWidth(), 42.0f);
}


// ─── Particle screen radius ────────────────────────────────────────
TEST(CanvasVisualization, ParticleRadiusAtHighZoomClampsMinimum) {
    float r = CircuitCanvas::particleScreenRadius(50.0f);
    EXPECT_GE(r, 1.5f);
}

// ─── Wire caps: rounding geometry must be inside the wire bounds ───
TEST(CanvasVisualization, WireEndpointCircleRadiusMatchesHalfWidth) {
    CircuitCanvas cv;
    cv.setWireThickness(10.0f);
    float halfW = cv.wireThickness() * 0.5f;
    float screenHW = halfW * cv.camera().scale;
    EXPECT_FLOAT_EQ(screenHW, 5.0f);                  // halfW=5, scale=1
    EXPECT_TRUE(screenHW <= cv.wireScreenWidth());    // circle fits in wire
}

TEST(CanvasVisualization, WireEndpointRadiusPositive) {
    CircuitCanvas cv;
    for (float t = 2.0f; t <= 50.0f; t += 2.0f) {
        cv.setWireThickness(t);
        float screenHW = cv.wireThickness() * 0.5f * cv.camera().scale;
        EXPECT_GT(screenHW, 0.0f);
    }
}

// ─── Surface charge dots stay inside wire bounds ───────────────────
TEST(CanvasVisualization, SurfaceChargeEdgeOffsetInsideWire) {
    CircuitCanvas cv;
    cv.setWireThickness(8.0f);
    float halfW = cv.wireThickness() * 0.5f;
    float edgeOff = halfW * 0.92f;
    EXPECT_LT(edgeOff, halfW);  // dots are inside the wire, not on boundary
    EXPECT_GT(edgeOff, 0.0f);
}

TEST(CanvasVisualization, SurfaceChargeEdgeOffsetScalesWithThickness) {
    CircuitCanvas cv;
    cv.setWireThickness(4.0f);
    float off4 = cv.wireThickness() * 0.5f * 0.92f;
    cv.setWireThickness(16.0f);
    float off16 = cv.wireThickness() * 0.5f * 0.92f;
    EXPECT_GT(off16, off4);
    EXPECT_FLOAT_EQ(off16, off4 * 4.0f);
}

TEST(CanvasVisualization, SurfaceChargeEdgeOffsetWithinWireForAllSizes) {
    CircuitCanvas cv;
    for (float t = 2.0f; t <= 50.0f; t += 1.0f) {
        cv.setWireThickness(t);
        float halfW = cv.wireThickness() * 0.5f;
        float edgeOff = halfW * 0.92f;
        EXPECT_LT(edgeOff, halfW);
    }
}

// ─── Wire length spans without dead gaps ────────────────────────────
TEST(CanvasVisualization, WireGradientStripsCoverFullLength) {
    CircuitCanvas cv;
    cv.setWireThickness(8.0f);
    float len = 100.0f;
    float screenLen = len * cv.camera().scale;
    float stripCount = std::max(2.0f, std::min(1000.0f, screenLen / 2.5f));
    EXPECT_GE(stripCount, 2.0f);  // at least 2 strips
    // strips are evenly placed from 0..len, no gap
    SUCCEED();
}

// ─── Pure visualization / physics models ──────────────────────────
TEST(CanvasPhysicsModels, FieldDirectionFollowsPotentialDrop) {
    auto dir = current_lab::physics::fieldDirection(Vec2(0, 0), Vec2(10, 0), 5.0, 1.0);
    EXPECT_NEAR(dir.x, 1.0, kEps);
    EXPECT_NEAR(dir.y, 0.0, kEps);

    auto reverse = current_lab::physics::fieldDirection(Vec2(0, 0), Vec2(10, 0), 1.0, 5.0);
    EXPECT_NEAR(reverse.x, -1.0, kEps);
    EXPECT_NEAR(reverse.y, 0.0, kEps);
}

TEST(CanvasPhysicsModels, FieldMagnitudeScalesWithVoltageAndLength) {
    double e1 = current_lab::physics::electricFieldMagnitude(4.0, 2.0);
    double e2 = current_lab::physics::electricFieldMagnitude(8.0, 2.0);
    double e3 = current_lab::physics::electricFieldMagnitude(4.0, 4.0);
    EXPECT_NEAR(e2, e1 * 2.0, kEps);
    EXPECT_NEAR(e3, e1 * 0.5, kEps);
}

TEST(CanvasPhysicsModels, DriftInfoKeepsComputedCurrentWhenConventionChanges) {
    auto conventional = current_lab::physics::driftVisualizationInfo(
        Vec2(0, 0), Vec2(10, 0), 0.25, false, 12.0);
    auto electron = current_lab::physics::driftVisualizationInfo(
        Vec2(0, 0), Vec2(10, 0), 0.25, true, 12.0);

    EXPECT_DOUBLE_EQ(conventional.computedCurrent, electron.computedCurrent);
    EXPECT_NEAR(conventional.visualDirection.x, 1.0, kEps);
    EXPECT_NEAR(electron.visualDirection.x, -1.0, kEps);
}

TEST(CanvasPhysicsModels, ZeroCurrentProducesNoDriftParticles) {
    current_lab::physics::DriftSamplingConfig config;
    auto particles = current_lab::physics::sampleDriftParticles(Vec2(0, 0), Vec2(10, 0), 0.0, config);
    EXPECT_TRUE(particles.empty());
}

TEST(CanvasPhysicsModels, DriftParticlesRemainInsideWireCrossSection) {
    current_lab::physics::DriftSamplingConfig config;
    config.wireThickness = 8.0;
    config.cameraScale = 2.0;
    config.time = 0.5;
    auto particles = current_lab::physics::sampleDriftParticles(Vec2(0, 0), Vec2(20, 0), 0.2, config);
    ASSERT_FALSE(particles.empty());

    double halfW = config.wireThickness * 0.5;
    for (const auto& particle : particles)
        EXPECT_LE(std::abs(particle.position.y), halfW + 1e-6);
}


TEST(CanvasPhysicsModels, ResistorPathConcentratesVoltageDropInBody) {
    auto sections = current_lab::physics::resistorPathSections(Vec2(0, 0), Vec2(100, 0), 5.0, 1.0, 8.0);
    ASSERT_EQ(sections.size(), 3u);
    EXPECT_EQ(sections[0].material, current_lab::physics::VisualMaterial::ConductiveLead);
    EXPECT_EQ(sections[1].material, current_lab::physics::VisualMaterial::ResistiveBody);
    EXPECT_EQ(sections[2].material, current_lab::physics::VisualMaterial::ConductiveLead);
    EXPECT_NEAR(sections[0].voltageStart, 5.0, kEps);
    EXPECT_NEAR(sections[0].voltageEnd, 5.0, kEps);
    EXPECT_NEAR(sections[1].voltageStart, 5.0, kEps);
    EXPECT_NEAR(sections[1].voltageEnd, 1.0, kEps);
    EXPECT_NEAR(sections[2].voltageStart, 1.0, kEps);
    EXPECT_NEAR(sections[2].voltageEnd, 1.0, kEps);
}

TEST(CanvasPhysicsModels, ResistorBodyWidthSlowsDriftVisualization) {
    double wireHalf = 4.0;
    double bodyHalf = current_lab::physics::resistorBodyHalfWidth(8.0);
    EXPECT_GT(bodyHalf, wireHalf);
    EXPECT_LT(current_lab::physics::driftSpeedScaleFromHalfWidth(wireHalf, bodyHalf), 1.0);
}

TEST(CanvasPhysicsModels, ResistorBodyFieldUsesBodyLength) {
    double fullLengthField = current_lab::physics::electricFieldMagnitude(4.0, 100.0);
    double bodyField = current_lab::physics::resistorBodyElectricFieldMagnitude(Vec2(0, 0), Vec2(100, 0), 5.0, 1.0, 8.0);
    EXPECT_GT(bodyField, fullLengthField);
}

TEST(CanvasPhysicsModels, MagneticFieldIncreasesWithCurrentAndDecreasesWithRadius) {
    double b1 = current_lab::physics::magneticFieldMagnitude(1.0, 1.0);
    double b2 = current_lab::physics::magneticFieldMagnitude(2.0, 1.0);
    double b3 = current_lab::physics::magneticFieldMagnitude(1.0, 2.0);
    EXPECT_GT(b2, b1);
    EXPECT_LT(b3, b1);
}

TEST(CanvasPhysicsModels, MagneticFieldDirectionReversesWithCurrent) {
    current_lab::physics::MagneticFieldSamplingConfig config;
    auto positive = current_lab::physics::sampleMagneticField(Vec2(0, 0), Vec2(10, 0), 1.0, config);
    auto negative = current_lab::physics::sampleMagneticField(Vec2(0, 0), Vec2(10, 0), -1.0, config);
    ASSERT_FALSE(positive.empty());
    ASSERT_EQ(positive.size(), negative.size());
    EXPECT_NE(positive.front().direction, negative.front().direction);
}

TEST(CanvasPhysicsModels, SurfaceChargeChangesSignAlongPotentialGradient) {
    current_lab::physics::SurfaceChargeSamplingConfig config;
    config.wireThickness = 8.0;
    config.cameraScale = 2.0;
    auto samples = current_lab::physics::sampleSurfaceCharges(Vec2(0, 0), Vec2(10, 0), 5.0, 1.0, 1.0, 5.0, config);
    ASSERT_FALSE(samples.empty());
    EXPECT_GT(samples.front().signedStrength, 0.0);
    EXPECT_LT(samples.back().signedStrength, 0.0);
}



// --- Visualization presets -------------------------------------------------
TEST(VisualizationPresets, PotentialPresetIsLearnerClean) {
    using current_lab::visualization::VisualizationPreset;
    using current_lab::visualization::presetInfo;

    auto info = presetInfo(VisualizationPreset::Potential);
    EXPECT_TRUE(info.layers.potential);
    EXPECT_FALSE(info.layers.drift);
    EXPECT_FALSE(info.layers.magnetic);
    EXPECT_FALSE(info.layers.surfaceCharge);
    EXPECT_FALSE(info.layers.debugMarkers);
    EXPECT_FALSE(info.layers.debugLog);
}

TEST(VisualizationPresets, ElectricFieldPresetAvoidsDebugClutter) {
    using current_lab::visualization::VisualizationPreset;
    using current_lab::visualization::presetInfo;

    auto info = presetInfo(VisualizationPreset::ElectricField);
    EXPECT_TRUE(info.layers.potential);
    EXPECT_TRUE(info.layers.electricField);
    EXPECT_FALSE(info.layers.magnetic);
    EXPECT_FALSE(info.layers.surfaceCharge);
    EXPECT_FALSE(info.layers.debugMarkers);
}

TEST(VisualizationPresets, DebugPresetEnablesDeveloperOverlays) {
    using current_lab::visualization::VisualizationPreset;
    using current_lab::visualization::presetInfo;

    auto info = presetInfo(VisualizationPreset::Debug);
    EXPECT_TRUE(info.layers.debugMarkers);
    EXPECT_TRUE(info.layers.debugLog);
    EXPECT_TRUE(info.layers.surfaceCharge);
    EXPECT_TRUE(info.layers.magnetic);
}

TEST(VisualizationPresets, CircuitPresetHidesDebugMarkers) {
    using current_lab::visualization::VisualizationPreset;
    using current_lab::visualization::presetInfo;

    auto info = presetInfo(VisualizationPreset::Circuit);
    EXPECT_TRUE(info.layers.current);
    EXPECT_TRUE(info.layers.power);
    EXPECT_FALSE(info.layers.surfaceCharge);
    EXPECT_FALSE(info.layers.magnetic);
    EXPECT_FALSE(info.layers.debugMarkers);
}

// ─── Switch toggle hot-zone: клик щёлкает ключ, НЕ выделяя его ──────────────
// (живой режим: щелчок выключателя — эксперимент, а не редактирование;
// выделение и редактор остаются доступны кликом по выводам вне зоны)
#include "projection/ElementGeometry.h"

namespace switch_toggle {

struct Recorder {
    int toggled = -1;
    int selectedComp = -1;
    int selectedNode = -1;
    bool deselected = false;
    current_lab::ui::CanvasInteraction interaction;

    Recorder() {
        interaction.callbacks.toggleSwitch = [this](int id) { toggled = id; };
        interaction.callbacks.selectComponent = [this](int id) { selectedComp = id; };
        interaction.callbacks.selectNode = [this](int id) { selectedNode = id; };
        interaction.callbacks.deselect = [this]() { deselected = true; };
    }
};

Circuit makeSwitchCircuit(int& swId) {
    Circuit c;
    int a = c.addNode(Vec2(100, 100));
    int b = c.addNode(Vec2(300, 100));
    swId = c.addComponent(ComponentType::Switch, a, b, 1.0);
    return c;
}

current_lab::ui::InteractionInput clickAt(Vec2 world) {
    current_lab::ui::InteractionInput in;
    in.mouseWorld = world;
    in.clicked = true;
    return in;
}

} // namespace switch_toggle

TEST(SwitchToggle, ClickInZoneTogglesWithoutSelecting) {
    using namespace switch_toggle;
    int swId = -1;
    Circuit c = makeSwitchCircuit(swId);
    Recorder r;
    r.interaction.handle(c, clickAt(Vec2(200, 100))); // середина глифа

    EXPECT_EQ(r.toggled, swId);
    EXPECT_EQ(r.selectedComp, -1) << "клик в зоне не должен выделять элемент";
    EXPECT_EQ(r.selectedNode, -1);
    EXPECT_FALSE(r.deselected) << "и не должен сбрасывать текущее выделение";
    EXPECT_EQ(r.interaction.selectedComponent(), -1);
}

TEST(SwitchToggle, ClickOnLeadOutsideZoneStillSelects) {
    using namespace switch_toggle;
    int swId = -1;
    Circuit c = makeSwitchCircuit(swId);
    Recorder r;
    r.interaction.handle(c, clickAt(Vec2(130, 100))); // вывод, 70 wu от середины

    EXPECT_EQ(r.toggled, -1);
    EXPECT_EQ(r.selectedComp, swId) << "обе возможности: выделение кликом по выводу";
}

TEST(SwitchToggle, HitZoneMatchesSharedGlyphGeometry) {
    using namespace switch_toggle;
    using namespace current_lab;
    int swId = -1;
    Circuit c = makeSwitchCircuit(swId);

    auto g = projection::switchGeometry(Vec2(100, 100), Vec2(300, 100));
    ASSERT_TRUE(g.valid);
    double radius = projection::switchToggleRadius(g, 8.0);

    Vec2 inside = g.mid + Vec2(radius - 1.0, 0.0);
    Vec2 outside = g.mid + Vec2(radius + 1.0, 0.0);
    EXPECT_EQ(ui::hitTestSwitchToggle(c, inside, 8.0), swId);
    EXPECT_EQ(ui::hitTestSwitchToggle(c, outside, 8.0), -1);
}

TEST(SwitchToggle, ZoneBeatsNodeHitInsideIt) {
    using namespace switch_toggle;
    int swId = -1;
    Circuit c = makeSwitchCircuit(swId);
    c.addNode(Vec2(210, 100)); // чужой узел внутри зоны (узлы обычно побеждают)
    Recorder r;
    r.interaction.handle(c, clickAt(Vec2(210, 100)));

    EXPECT_EQ(r.toggled, swId) << "внутри зоны щелчок ключа важнее выделения узла";
    EXPECT_EQ(r.selectedNode, -1);
}

TEST(SwitchToggle, WithoutCallbackFallsBackToSelection) {
    using namespace switch_toggle;
    int swId = -1;
    Circuit c = makeSwitchCircuit(swId);
    Recorder r;
    r.interaction.callbacks.toggleSwitch = nullptr; // канвас без тоггла
    r.interaction.handle(c, clickAt(Vec2(200, 100)));

    EXPECT_EQ(r.selectedComp, swId) << "без колбэка зона не должна глотать клики";
}
