#include <gtest/gtest.h>
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "projection/ProjectionBuilder.h"
#include "ui/DualViewState.h"

namespace {

using current_lab::projection::ProjectionKind;
using current_lab::projection::ViewParams;
using current_lab::projection::buildProjection;
using current_lab::projection::projectionHasComponent;
using current_lab::projection::projectionElement;

Circuit makeSeriesCircuit(int& resistorId)
{
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100), "GND");
    int n1 = c.addNode(Vec2(0, 0), "N1");
    int n2 = c.addNode(Vec2(120, 0), "N2");
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resistorId = c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    c.addComponent(ComponentType::Wire, n2, gnd, 0.0);
    return c;
}

} // namespace

TEST(DualViewState, SelectingCircuitComponentSelectsSharedComponentId)
{
    current_lab::ui::DualViewState state;
    state.select(current_lab::ui::DualViewPane::Circuit, 42);
    ASSERT_TRUE(state.selectedComponentId.has_value());
    EXPECT_EQ(*state.selectedComponentId, 42);
}

TEST(DualViewState, SelectingPhysicsComponentSelectsSharedComponentId)
{
    current_lab::ui::DualViewState state;
    state.select(current_lab::ui::DualViewPane::Physics, 7);
    ASSERT_TRUE(state.selectedComponentId.has_value());
    EXPECT_EQ(*state.selectedComponentId, 7);
}

TEST(DualViewState, PanSyncsCamerasWhenEnabled)
{
    current_lab::ui::DualViewState state;
    state.syncCameras = true;
    state.pan(current_lab::ui::DualViewPane::Circuit, Vec2(12, -4));
    EXPECT_TRUE(current_lab::ui::cameraApproximatelyEqual(state.circuitCamera, state.physicsCamera));
}

TEST(DualViewState, ZoomSyncsCamerasWhenEnabled)
{
    current_lab::ui::DualViewState state;
    state.syncCameras = true;
    state.zoomAt(current_lab::ui::DualViewPane::Physics, 1.25f, Vec2(100, 80));
    EXPECT_TRUE(current_lab::ui::cameraApproximatelyEqual(state.circuitCamera, state.physicsCamera));
}

TEST(DualViewState, CamerasMoveIndependentlyWhenSyncDisabled)
{
    current_lab::ui::DualViewState state;
    state.syncCameras = false;
    state.pan(current_lab::ui::DualViewPane::Circuit, Vec2(10, 0));
    EXPECT_FALSE(current_lab::ui::cameraApproximatelyEqual(state.circuitCamera, state.physicsCamera));
}

// The dual-view guarantees, asserted against the REAL render path: both panes
// are built by buildProjection from the same model + solution.
TEST(DualViewProjection, ParameterEditRecomputesBothProjectionValues)
{
    int resistorId = -1;
    Circuit circuit = makeSeriesCircuit(resistorId);
    CircuitSolver solver;
    ViewParams params;

    auto before = solver.solve(circuit);
    auto beforeSchematic = buildProjection(ProjectionKind::Schematic, circuit, &before, params);
    ASSERT_TRUE(projectionHasComponent(beforeSchematic, resistorId));
    const auto* beforeElement = projectionElement(beforeSchematic, resistorId);
    ASSERT_NE(beforeElement, nullptr);
    double currentBefore = beforeElement->current;

    Component* resistor = circuit.findComponent(resistorId);
    ASSERT_NE(resistor, nullptr);
    resistor->value = 2000.0;

    auto after = solver.solve(circuit);
    auto afterSchematic = buildProjection(ProjectionKind::Schematic, circuit, &after, params);
    auto afterPhysics = buildProjection(ProjectionKind::Physics, circuit, &after, params);

    const auto* schematicElement = projectionElement(afterSchematic, resistorId);
    const auto* physicsElement = projectionElement(afterPhysics, resistorId);
    ASSERT_NE(schematicElement, nullptr);
    ASSERT_NE(physicsElement, nullptr);

    EXPECT_LT(std::abs(schematicElement->current), std::abs(currentBefore));
    ASSERT_EQ(afterSchematic.elements.size(), afterPhysics.elements.size());
    EXPECT_DOUBLE_EQ(schematicElement->current, physicsElement->current);
    EXPECT_DOUBLE_EQ(schematicElement->voltageA, physicsElement->voltageA);
    EXPECT_DOUBLE_EQ(schematicElement->voltageB, physicsElement->voltageB);
}

TEST(DualViewProjection, DeleteRemovesComponentFromBothProjections)
{
    int resistorId = -1;
    Circuit circuit = makeSeriesCircuit(resistorId);
    CircuitSolver solver;
    ViewParams params;

    auto before = solver.solve(circuit);
    auto beforeProjection = buildProjection(ProjectionKind::Physics, circuit, &before, params);
    ASSERT_TRUE(projectionHasComponent(beforeProjection, resistorId));

    circuit.removeComponent(resistorId);
    auto after = solver.solve(circuit);
    auto afterSchematic = buildProjection(ProjectionKind::Schematic, circuit, &after, params);
    auto afterPhysics = buildProjection(ProjectionKind::Physics, circuit, &after, params);

    EXPECT_FALSE(projectionHasComponent(afterSchematic, resistorId));
    EXPECT_FALSE(projectionHasComponent(afterPhysics, resistorId));
    EXPECT_EQ(afterSchematic.elements.size(), afterPhysics.elements.size());
}

TEST(DualViewLayout, PhysicsPaneKeepsPositiveWidth)
{
    auto split = current_lab::ui::computeDualViewPaneSplit(700.0f, 8.0f);
    EXPECT_GT(split.circuitWidth, 0.0f);
    EXPECT_GT(split.physicsWidth, 0.0f);
    EXPECT_NEAR(split.circuitWidth + split.physicsWidth + 8.0f, 700.0f, 1.0f);
}

TEST(DualViewLayout, InspectorCollapsesBeforeStealingDualViewSpace)
{
    auto layout = current_lab::ui::computeDualViewLayout(820.0f, 320.0f, true, true, 8.0f);
    EXPECT_FALSE(layout.showInspector);
    EXPECT_FLOAT_EQ(layout.collapsedInspectorWidth, 40.0f);
    EXPECT_GT(layout.canvasWidth, 700.0f);
}

TEST(DualViewLayout, InspectorShowsWhenThereIsEnoughRoom)
{
    auto layout = current_lab::ui::computeDualViewLayout(1280.0f, 320.0f, true, true, 8.0f);
    EXPECT_TRUE(layout.showInspector);
    EXPECT_GT(layout.inspectorWidth, 0.0f);
    EXPECT_GT(layout.canvasWidth, 620.0f);
}
