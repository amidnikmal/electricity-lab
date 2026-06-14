#include <gtest/gtest.h>
#include <cmath>
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

        EXPECT_GE(current, previous); // нестрогий рост: на плато температура не меняется
        EXPECT_LE(current, steady);   // без выброса выше установившегося значения
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

// Проверяем новую τ: после τ секунд температура должна достичь ≈63% установившегося значения
TEST(ThermalModel, TimeConstantMatchesRC) {
    constexpr double kPower = 2.0;
    constexpr double kDt = 0.025; // τ/200 — высокая точность дискретизации
    ThermalInput input = makePoweredResistorInput(kPower);
    ThermalState state;

    const double tau = kThermalResistance * kThermalCapacitance;
    const double steadyDelta = kPower * kThermalResistance;
    const int steps = static_cast<int>(tau / kDt);

    for (int i = 0; i < steps; ++i)
        stepThermal(state, input.circuit, input.solution, kDt);

    double deltaT = temperatureFor(state, input.componentId) - kAmbientTemperature;
    double fraction = deltaT / steadyDelta;

    // После 1τ: 1 − 1/e ≈ 0.6321; допуск 0.02 на дискретизацию обратным Эйлером
    EXPECT_NEAR(fraction, 1.0 - 1.0 / std::exp(1.0), 0.02);
}

// Переопределение R_th меняет установившуюся температуру: T_ss = T_amb + P * R_th
TEST(ThermalModel, OverrideThermalResistanceChangesSteadyState) {
    constexpr double kPower = 1.0;
    constexpr double kDt = 0.1;
    constexpr double kOverrideRth = 10.0; // вдвое больше глобального (5.0)

    ThermalInput input = makePoweredResistorInput(kPower);
    ThermalConfig config;
    config.rThOverride[input.componentId] = kOverrideRth;

    ThermalState state;
    const double steadyOverride = kAmbientTemperature + kPower * kOverrideRth;

    for (int i = 0; i < 100000; ++i)
        stepThermal(state, input.circuit, input.solution, kDt, config);

    EXPECT_NEAR(temperatureFor(state, input.componentId), steadyOverride, 1e-3);
}

// Переопределение C_th меняет скорость нагрева: меньшая C_th — быстрее, большая — медленнее
TEST(ThermalModel, OverrideThermalCapacitanceChangesHeatingRate) {
    constexpr double kPower = 1.0;
    constexpr double kDt = 0.1;
    constexpr int kSteps = 50; // 5 с модельного времени

    ThermalInput input = makePoweredResistorInput(kPower);

    // Глобальная C_th (1.0, τ = 5.0 с)
    ThermalState stateDef;
    for (int i = 0; i < kSteps; ++i)
        stepThermal(stateDef, input.circuit, input.solution, kDt);
    double Tdef = temperatureFor(stateDef, input.componentId);

    // Меньшая C_th (0.2, τ = 1.0 с) — быстрее нагрев
    ThermalState stateFast;
    ThermalConfig cfgFast;
    cfgFast.cThOverride[input.componentId] = 0.2;
    for (int i = 0; i < kSteps; ++i)
        stepThermal(stateFast, input.circuit, input.solution, kDt, cfgFast);
    double Tfast = temperatureFor(stateFast, input.componentId);

    // Большая C_th (4.0, τ = 20.0 с) — медленнее нагрев
    ThermalState stateSlow;
    ThermalConfig cfgSlow;
    cfgSlow.cThOverride[input.componentId] = 4.0;
    for (int i = 0; i < kSteps; ++i)
        stepThermal(stateSlow, input.circuit, input.solution, kDt, cfgSlow);
    double Tslow = temperatureFor(stateSlow, input.componentId);

    EXPECT_GT(Tfast, Tdef);
    EXPECT_GT(Tdef, Tslow);
}
