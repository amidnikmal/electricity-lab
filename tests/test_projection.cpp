#include <gtest/gtest.h>
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "projection/ProjectionBuilder.h"
#include "ui/CanvasInteraction.h"

namespace {

using namespace current_lab::projection;

Circuit makeSeriesCircuit(int& sourceId, int& resistorId, int& wireId)
{
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100), "GND");
    int n1 = c.addNode(Vec2(0, 0), "N1");
    int n2 = c.addNode(Vec2(120, 0), "N2");
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    sourceId = c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    resistorId = c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    wireId = c.addComponent(ComponentType::Wire, n2, gnd, 0.0);
    return c;
}

ViewParams physicsParams()
{
    ViewParams p;
    p.layers.current = true;
    p.layers.potential = true;
    p.layers.drift = true;
    p.layers.electricField = true;
    p.layers.heat = true;
    p.layers.power = true;
    p.layers.surfaceCharge = true;
    return p;
}

} // namespace

TEST(ProjectionBuilder, AllKindsShareTheSameComponentIds)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);
    CircuitSolver solver;
    auto solution = solver.solve(circuit);
    ViewParams params = physicsParams();

    for (auto kind : {ProjectionKind::Schematic, ProjectionKind::Physics, ProjectionKind::Mechanical}) {
        auto result = buildProjection(kind, circuit, &solution, params);
        EXPECT_EQ(result.elements.size(), circuit.components.size());
        EXPECT_TRUE(projectionHasComponent(result, sourceId));
        EXPECT_TRUE(projectionHasComponent(result, resistorId));
        EXPECT_TRUE(projectionHasComponent(result, wireId));
    }
}

TEST(ProjectionBuilder, ElementValuesComeFromSolver)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);
    CircuitSolver solver;
    auto solution = solver.solve(circuit);

    auto result = buildProjection(ProjectionKind::Physics, circuit, &solution, physicsParams());
    const auto* resistor = projectionElement(result, resistorId);
    ASSERT_NE(resistor, nullptr);

    // 5 V across ~1 kOhm series loop -> about 5 mA, dV ~ 5 V.
    EXPECT_NEAR(resistor->current, 0.005, 5e-4);
    EXPECT_NEAR(resistor->voltageA - resistor->voltageB, 5.0, 0.05);
}

TEST(ProjectionBuilder, RemovingComponentRemovesItsPrimitivesEverywhere)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);
    CircuitSolver solver;
    auto solution = solver.solve(circuit);
    ViewParams params = physicsParams();

    auto fullSchematic = buildProjection(ProjectionKind::Schematic, circuit, &solution, params);
    auto fullPhysics = buildProjection(ProjectionKind::Physics, circuit, &solution, params);

    circuit.removeComponent(resistorId);
    auto cutSolution = solver.solve(circuit);
    auto cutSchematic = buildProjection(ProjectionKind::Schematic, circuit, &cutSolution, params);
    auto cutPhysics = buildProjection(ProjectionKind::Physics, circuit, &cutSolution, params);

    EXPECT_FALSE(projectionHasComponent(cutSchematic, resistorId));
    EXPECT_FALSE(projectionHasComponent(cutPhysics, resistorId));
    EXPECT_LT(cutSchematic.prims.totalCount(), fullSchematic.prims.totalCount());
    EXPECT_LT(cutPhysics.prims.totalCount(), fullPhysics.prims.totalCount());
}

TEST(ProjectionBuilder, GradientPrimitivesCarrySolverVoltages)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);
    CircuitSolver solver;
    auto solution = solver.solve(circuit);

    auto result = buildProjection(ProjectionKind::Physics, circuit, &solution, physicsParams());
    ASSERT_FALSE(result.prims.gradients.empty());

    double vMin = solution.nodePotentials.front().potential;
    double vMax = vMin;
    for (const auto& np : solution.nodePotentials) {
        vMin = std::min(vMin, np.potential);
        vMax = std::max(vMax, np.potential);
    }

    for (const auto& grad : result.prims.gradients) {
        EXPECT_DOUBLE_EQ(grad.vMin, vMin);
        EXPECT_DOUBLE_EQ(grad.vMax, vMax);
        EXPECT_GE(grad.vA, vMin - 1e-9);
        EXPECT_LE(grad.vA, vMax + 1e-9);
        EXPECT_GE(grad.vB, vMin - 1e-9);
        EXPECT_LE(grad.vB, vMax + 1e-9);
    }
}

TEST(ProjectionBuilder, PhysicsLayersRespectFlags)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);
    CircuitSolver solver;
    auto solution = solver.solve(circuit);

    ViewParams bare;
    bare.layers = {}; // everything off
    auto bareResult = buildProjection(ProjectionKind::Physics, circuit, &solution, bare);
    EXPECT_TRUE(bareResult.prims.particles.empty());
    EXPECT_TRUE(bareResult.prims.arrows.empty());
    EXPECT_FALSE(bareResult.prims.legend.show);

    ViewParams drift;
    drift.layers = {};
    drift.layers.drift = true;
    auto driftResult = buildProjection(ProjectionKind::Physics, circuit, &solution, drift);
    EXPECT_FALSE(driftResult.prims.particles.empty());

    ViewParams field;
    field.layers = {};
    field.layers.current = true;
    auto fieldResult = buildProjection(ProjectionKind::Physics, circuit, &solution, field);
    EXPECT_FALSE(fieldResult.prims.arrows.empty());
}

TEST(ProjectionBuilder, SchematicStaysCleanRegardlessOfPhysicsFlags)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);
    CircuitSolver solver;
    auto solution = solver.solve(circuit);

    auto result = buildProjection(ProjectionKind::Schematic, circuit, &solution, physicsParams());
    EXPECT_TRUE(result.prims.particles.empty()); // no drift / surface charge dots
    EXPECT_TRUE(result.prims.glows.empty());     // no field backdrop
    EXPECT_FALSE(result.prims.legend.show);      // no potential legend
}

TEST(ProjectionBuilder, SelectionEmitsHighlightInEveryProjection)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);
    CircuitSolver solver;
    auto solution = solver.solve(circuit);

    ViewParams params = physicsParams();
    auto plain = buildProjection(ProjectionKind::Schematic, circuit, &solution, params);
    params.selectedComponent = resistorId;
    auto selectedSchematic = buildProjection(ProjectionKind::Schematic, circuit, &solution, params);
    auto selectedPhysics = buildProjection(ProjectionKind::Physics, circuit, &solution, params);

    EXPECT_GT(selectedSchematic.prims.quads.size(), plain.prims.quads.size());
    EXPECT_GT(selectedPhysics.prims.quads.size(), 0u);
}

// Interaction layer: editing goes through the shared model only.
TEST(CanvasInteraction, ClickSelectsComponentThroughCallbacks)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);

    current_lab::ui::CanvasInteraction interaction;
    int selected = -1;
    interaction.callbacks.selectComponent = [&](int id) { selected = id; };

    current_lab::ui::InteractionInput input;
    input.mouseWorld = Vec2(60, 0); // middle of the resistor span
    input.clicked = true;
    interaction.handle(circuit, input);

    EXPECT_EQ(selected, resistorId);
    EXPECT_EQ(interaction.selectedComponent(), resistorId);
}

TEST(CanvasInteraction, PlaceWireCreatesComponentBetweenNodes)
{
    Circuit circuit;
    int n0 = circuit.addNode(Vec2(0, 0));
    int n1 = circuit.addNode(Vec2(100, 0));

    current_lab::ui::CanvasInteraction interaction;
    interaction.setMode(EditorMode::PlaceWire);
    interaction.callbacks.createComponent = [&](int from, int to, ComponentType type, double value) {
        circuit.addComponent(type, from, to, value);
    };

    current_lab::ui::InteractionInput press;
    press.mouseWorld = Vec2(0, 0);
    press.clicked = true;
    interaction.handle(circuit, press);
    EXPECT_EQ(interaction.placeFromNode(), n0);

    current_lab::ui::InteractionInput release;
    release.mouseWorld = Vec2(100, 0);
    release.released = true;
    interaction.handle(circuit, release);

    ASSERT_EQ(circuit.components.size(), 1u);
    EXPECT_EQ(circuit.components[0].type, ComponentType::Wire);
    EXPECT_EQ(circuit.components[0].nodeA, n0);
    EXPECT_EQ(circuit.components[0].nodeB, n1);
    EXPECT_EQ(interaction.placeFromNode(), -1);
}

TEST(CanvasInteraction, DeleteSelectedFiresCallback)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);

    current_lab::ui::CanvasInteraction interaction;
    bool deleteFired = false;
    interaction.callbacks.deleteSelected = [&]() { deleteFired = true; };
    interaction.setSelected(-1, resistorId);

    current_lab::ui::InteractionInput input;
    input.deletePressed = true;
    interaction.handle(circuit, input);

    EXPECT_TRUE(deleteFired);
    EXPECT_EQ(interaction.selectedComponent(), -1);
}

TEST(CanvasInteraction, HitTestFindsNodeAndComponent)
{
    int sourceId, resistorId, wireId;
    Circuit circuit = makeSeriesCircuit(sourceId, resistorId, wireId);

    EXPECT_GE(current_lab::ui::hitTestNode(circuit, Vec2(0, 0)), 0);
    EXPECT_EQ(current_lab::ui::hitTestNode(circuit, Vec2(500, 500)), -1);
    EXPECT_EQ(current_lab::ui::hitTestComponent(circuit, Vec2(60, 0)), resistorId);
    EXPECT_EQ(current_lab::ui::hitTestComponent(circuit, Vec2(500, 500)), -1);
}
