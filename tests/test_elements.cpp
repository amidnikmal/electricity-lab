#include <gtest/gtest.h>
#include <cmath>
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "projection/ProjectionBuilder.h"
#include "projection/ElementGeometry.h"
#include "ui/CanvasInteraction.h"

namespace {

using namespace current_lab::projection;

struct RcFixture {
    Circuit c;
    int capId = -1;
};

RcFixture makeRc() {
    RcFixture f;
    int gnd = f.c.addNode(Vec2(0, 100));
    int n1 = f.c.addNode(Vec2(0, 0));
    int n2 = f.c.addNode(Vec2(200, 0));
    f.c.groundNodeId = gnd;
    f.c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    f.c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    f.c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    f.capId = f.c.addComponent(ComponentType::Capacitor, n2, gnd, 1e-3);
    return f;
}

ViewParams allLayers() {
    ViewParams p;
    p.layers.current = true;
    p.layers.potential = true;
    p.layers.drift = true;
    p.layers.electricField = true;
    p.layers.heat = true;
    p.layers.power = true;
    p.layers.magnetic = true;
    p.layers.surfaceCharge = true;
    return p;
}

} // namespace

TEST(ElementGeometry, CapacitorPlatesPerpendicularAndSymmetric) {
    auto g = capacitorGeometry(Vec2(0, 0), Vec2(100, 0), 8.0);
    ASSERT_TRUE(g.valid);
    EXPECT_NEAR(g.mid.x, 50.0, 1e-9);
    EXPECT_LT(g.leadAEnd.x, g.leadBEnd.x);
    EXPECT_NEAR(g.leadBEnd.x - g.leadAEnd.x, g.gap, 1e-9);
    // Plates are vertical for a horizontal capacitor.
    EXPECT_NEAR(g.plateATop.x, g.plateABottom.x, 1e-9);
    EXPECT_NEAR(g.plateATop.y - g.plateABottom.y, 2.0 * g.plateHalf, 1e-9);
}

TEST(ElementGeometry, InductorCoilCenteredWithBumps) {
    auto g = inductorGeometry(Vec2(0, 0), Vec2(100, 0), 8.0);
    ASSERT_TRUE(g.valid);
    EXPECT_EQ(g.bumps, 4);
    EXPECT_GT(g.bumpRadius, 0.0);
    auto arc = inductorBumpArc(g, 0);
    ASSERT_FALSE(arc.empty());
    for (const auto& p : arc)
        EXPECT_GE(p.y, -1e-9); // bump rises above the axis (perp = +y here)
}

TEST(ElementProjection, CapacitorChargesInTransientAndShowsStoredEnergy) {
    RcFixture f = makeRc();
    CircuitSolver solver;
    TransientState state;
    for (int i = 0; i < 2000; ++i)
        solver.stepTransient(f.c, state, 1e-3); // 2 tau

    auto solution = solver.solveTransientSnapshot(f.c, state);
    auto result = buildProjection(ProjectionKind::Physics, f.c, &solution, allLayers());

    const auto* cap = projectionElement(result, f.capId);
    ASSERT_NE(cap, nullptr);
    double vc = cap->voltageA - cap->voltageB;
    EXPECT_NEAR(vc, 5.0 * (1.0 - std::exp(-2.0)), 0.05);
    EXPECT_NEAR(cap->storedEnergy, 0.5 * 1e-3 * vc * vc, 1e-6);
}

TEST(ElementProjection, CapacitorSymbolAppearsInSchematic) {
    RcFixture f = makeRc();
    CircuitSolver solver;
    auto solution = solver.solve(f.c);
    auto result = buildProjection(ProjectionKind::Schematic, f.c, &solution, ViewParams{});

    // Plate lines are screen-space width-3 lines; find at least two of them.
    int plateLines = 0;
    for (const auto& line : result.prims.lines) {
        if (line.screenSpaceWidth && std::abs(line.width - 3.0) < 1e-9)
            ++plateLines;
    }
    EXPECT_GE(plateLines, 2);

    bool hasFaradLabel = false;
    for (const auto& label : result.prims.labels)
        hasFaradLabel = hasFaradLabel || label.text.find("mF") != std::string::npos ||
                        label.text.find("uF") != std::string::npos;
    EXPECT_TRUE(hasFaradLabel);
}

TEST(ElementProjection, InductorSymbolHasCoilBumps) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 10.0);
    c.addComponent(ComponentType::Inductor, n2, gnd, 1.0);

    CircuitSolver solver;
    auto solution = solver.solve(c);
    auto result = buildProjection(ProjectionKind::Schematic, c, &solution, ViewParams{});

    EXPECT_GE(result.prims.polylines.size(), 4u); // 4 coil bumps

    bool hasHenryLabel = false;
    for (const auto& label : result.prims.labels)
        hasHenryLabel = hasHenryLabel || label.text.find(" H") != std::string::npos;
    EXPECT_TRUE(hasHenryLabel);
}

TEST(ElementProjection, ChargedCapacitorShowsPlateChargesInPhysicsView) {
    RcFixture f = makeRc();
    CircuitSolver solver;
    TransientState state;
    state.capVoltage[f.capId] = 4.0;
    auto solution = solver.solveTransientSnapshot(f.c, state);

    ViewParams params = allLayers();
    auto charged = buildProjection(ProjectionKind::Physics, f.c, &solution, params);

    TransientState empty;
    auto dischargedSolution = solver.solveTransientSnapshot(f.c, empty);
    auto discharged = buildProjection(ProjectionKind::Physics, f.c, &dischargedSolution, params);

    // A charged capacitor adds plate-charge particles and an energy glow.
    EXPECT_GT(charged.prims.glows.size(), discharged.prims.glows.size());
}

TEST(ElementProjection, InductorEnergyGrowsWithCurrent) {
    Circuit c;
    int gnd = c.addNode(Vec2(0, 100));
    int n1 = c.addNode(Vec2(0, 0));
    int n2 = c.addNode(Vec2(200, 0));
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    c.addComponent(ComponentType::Resistor, n1, n2, 10.0);
    int indId = c.addComponent(ComponentType::Inductor, n2, gnd, 1.0);

    CircuitSolver solver;
    TransientState state;
    for (int i = 0; i < 1000; ++i)
        solver.stepTransient(c, state, 1e-4); // 1 tau

    auto solution = solver.solveTransientSnapshot(c, state);
    auto result = buildProjection(ProjectionKind::Physics, c, &solution, allLayers());
    const auto* ind = projectionElement(result, indId);
    ASSERT_NE(ind, nullptr);

    double il = ind->current;
    EXPECT_NEAR(il, 0.5 * (1.0 - std::exp(-1.0)), 0.01);
    EXPECT_NEAR(ind->storedEnergy, 0.5 * 1.0 * il * il, 1e-6);
}

TEST(ElementDefaults, CapacitorAndInductorHavePhysicalDefaults) {
    EXPECT_DOUBLE_EQ(current_lab::ui::defaultValueFor(ComponentType::Capacitor), 1e-3);
    EXPECT_DOUBLE_EQ(current_lab::ui::defaultValueFor(ComponentType::Inductor), 1.0);
}

TEST(ElementInteraction, PlaceCapacitorModeCreatesCapacitor) {
    Circuit circuit;
    int n0 = circuit.addNode(Vec2(0, 0));
    int n1 = circuit.addNode(Vec2(100, 0));

    current_lab::ui::CanvasInteraction interaction;
    interaction.setMode(EditorMode::PlaceCapacitor);
    interaction.callbacks.createComponent = [&](int from, int to, ComponentType type, double value) {
        circuit.addComponent(type, from, to, value);
    };

    current_lab::ui::InteractionInput press;
    press.mouseWorld = Vec2(0, 0);
    press.clicked = true;
    interaction.handle(circuit, press);

    current_lab::ui::InteractionInput release;
    release.mouseWorld = Vec2(100, 0);
    release.released = true;
    interaction.handle(circuit, release);

    ASSERT_EQ(circuit.components.size(), 1u);
    EXPECT_EQ(circuit.components[0].type, ComponentType::Capacitor);
    EXPECT_DOUBLE_EQ(circuit.components[0].value, 1e-3);
    EXPECT_EQ(circuit.components[0].nodeA, n0);
    EXPECT_EQ(circuit.components[0].nodeB, n1);
}
