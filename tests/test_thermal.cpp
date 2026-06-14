#include <gtest/gtest.h>
#include "circuit/Circuit.h"
#include "physics/PhysicalUnits.h"
#include "physics/ThermalModel.h"
#include "solver/CircuitSolver.h"

namespace {

using namespace current_lab::physics;

struct ThermalInput {
    Circuit circuit;
    CircuitSolution solution;
    int componentId = -1;
};

ThermalInput makeSingleComponentInput(ComponentType type, double value, double power) {
    ThermalInput input;
    int gnd = input.circuit.addNode(Vec2(0, 100), "GND");
    int n1 = input.circuit.addNode(Vec2(0, 0), "N1");
    input.circuit.groundNodeId = gnd;
    input.circuit.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    input.componentId = input.circuit.addComponent(type, n1, gnd, value);
    input.solution.branches.push_back(BranchResult{input.componentId, 0.0, 0.0, power});
    return input;
}

ThermalInput makePoweredResistorInput(double power) {
    return makeSingleComponentInput(ComponentType::Resistor, 100.0, power);
}

} // namespace

TEST(ThermalModel, TemperatureForFallsBackToAmbientAndResetClearsState) {
    ThermalState state;

    EXPECT_DOUBLE_EQ(temperatureFor(state, 42), kAmbientTemperature);

    state.temperature[42] = kAmbientTemperature + 10.0;
    state.time = 7.0;
    state.reset();

    EXPECT_DOUBLE_EQ(temperatureFor(state, 42), kAmbientTemperature);
    EXPECT_DOUBLE_EQ(state.time, 0.0);
}

TEST(ThermalModel, PoweredResistorHeatsMonotonicallyWithoutOvershoot) {
    constexpr double kPower = 2.0;
    constexpr double kDt = 1.0;
    ThermalInput input = makePoweredResistorInput(kPower);
    ThermalState state;

    const double steady = kAmbientTemperature + kPower * kThermalResistance;
    double previous = temperatureFor(state, input.componentId);

    for (int i = 0; i < 200; ++i) {
        stepThermal(state, input.circuit, input.solution, kDt);
        double current = temperatureFor(state, input.componentId);

        EXPECT_GT(current, previous);
        EXPECT_LE(current, steady);
        previous = current;
    }
}

TEST(ThermalModel, PoweredResistorConvergesToSteadyStateAndSteadyStateIsFixedPoint) {
    constexpr double kPower = 1.5;
    constexpr double kDt = 1.0;
    ThermalInput input = makePoweredResistorInput(kPower);
    ThermalState state;

    const double steady = kAmbientTemperature + kPower * kThermalResistance;
    for (int i = 0; i < 100000; ++i)
        stepThermal(state, input.circuit, input.solution, kDt);

    EXPECT_NEAR(temperatureFor(state, input.componentId), steady, 1e-3);

    ThermalState steadyState;
    steadyState.temperature[input.componentId] = steady;
    stepThermal(steadyState, input.circuit, input.solution, kDt);

    EXPECT_NEAR(temperatureFor(steadyState, input.componentId), steady, 1e-12);
}

TEST(ThermalModel, NonDissipatingComponentWithNonzeroBranchPowerStaysAmbient) {
    ThermalInput input = makeSingleComponentInput(ComponentType::VoltageSource, 5.0, 10.0);
    ThermalState state;

    for (int i = 0; i < 20; ++i)
        stepThermal(state, input.circuit, input.solution, 1.0);

    EXPECT_NEAR(temperatureFor(state, input.componentId), kAmbientTemperature, 1e-12);
}

TEST(ThermalModel, TimeAccumulatesAcrossSteps) {
    ThermalInput input = makePoweredResistorInput(0.25);
    ThermalState state;

    stepThermal(state, input.circuit, input.solution, 0.25);
    EXPECT_NEAR(state.time, 0.25, 1e-12);

    stepThermal(state, input.circuit, input.solution, 0.75);
    EXPECT_NEAR(state.time, 1.0, 1e-12);
}

TEST(ThermalModel, CelsiusConvertsKelvinOffset) {
    EXPECT_NEAR(celsius(kAmbientTemperature), 20.0, 1e-12);
}
