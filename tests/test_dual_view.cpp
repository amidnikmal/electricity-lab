#include <gtest/gtest.h>
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "ui/DualViewProjection.h"
#include "ui/DualViewState.h"

namespace {

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

const BranchResult* branchFor(const CircuitSolution& solution, int componentId)
{
    for (const auto& branch : solution.branches) {
        if (branch.componentId == componentId)
            return &branch;
    }
    return nullptr;
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

TEST(DualViewProjection, ParameterEditRecomputesBothProjectionValues)
{
    int resistorId = -1;
    Circuit circuit = makeSeriesCircuit(resistorId);
    CircuitSolver solver;
    auto before = solver.solve(circuit);
    auto beforeProjection = current_lab::ui::buildDualViewProjection(circuit, &before);
    ASSERT_TRUE(current_lab::ui::projectionHasComponent(beforeProjection, resistorId));
    ASSERT_NE(branchFor(before, resistorId), nullptr);
    double currentBefore = branchFor(before, resistorId)->current;

    Component* resistor = circuit.findComponent(resistorId);
    ASSERT_NE(resistor, nullptr);
    resistor->value = 2000.0;

    auto after = solver.solve(circuit);
    auto afterProjection = current_lab::ui::buildDualViewProjection(circuit, &after);
    ASSERT_TRUE(current_lab::ui::projectionHasComponent(afterProjection, resistorId));
    ASSERT_NE(branchFor(after, resistorId), nullptr);
    double currentAfter = branchFor(after, resistorId)->current;

    EXPECT_LT(std::abs(currentAfter), std::abs(currentBefore));
    ASSERT_EQ(afterProjection.circuitElements.size(), afterProjection.physicsElements.size());
    EXPECT_EQ(afterProjection.circuitElements[2].componentId, afterProjection.physicsElements[2].componentId);
    EXPECT_DOUBLE_EQ(afterProjection.circuitElements[2].current, afterProjection.physicsElements[2].current);
}

TEST(DualViewProjection, DeleteRemovesComponentFromBothProjections)
{
    int resistorId = -1;
    Circuit circuit = makeSeriesCircuit(resistorId);
    CircuitSolver solver;
    auto before = solver.solve(circuit);
    auto beforeProjection = current_lab::ui::buildDualViewProjection(circuit, &before);
    ASSERT_TRUE(current_lab::ui::projectionHasComponent(beforeProjection, resistorId));

    circuit.removeComponent(resistorId);
    auto after = solver.solve(circuit);
    auto afterProjection = current_lab::ui::buildDualViewProjection(circuit, &after);

    EXPECT_FALSE(current_lab::ui::projectionHasComponent(afterProjection, resistorId));
    EXPECT_EQ(afterProjection.circuitElements.size(), afterProjection.physicsElements.size());
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
