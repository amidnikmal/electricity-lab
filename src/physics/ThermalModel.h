#pragma once

#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "physics/PhysicalUnits.h"
#include "physics/PowerModel.h"

#include <unordered_map>

namespace current_lab::physics {

// Lumped RC thermal model, DISPLAY-ONLY (no R(T) feedback). Mirror of
// TransientState: temperature per componentId, advanced by backward Euler
// on the same dt as the electrical transient.
//   C_th dT/dt = P_diss - (T - T_amb)/R_th
// Steady state: T = T_amb + P_diss * R_th.
struct ThermalState {
    double time = 0.0;
    std::unordered_map<int, double> temperature; // K, by componentId
    void reset() { *this = ThermalState{}; }
};

// Temperature of a component in Kelvin; kAmbientTemperature if absent.
inline double temperatureFor(const ThermalState& state, int componentId) {
    auto it = state.temperature.find(componentId);
    return it == state.temperature.end() ? kAmbientTemperature : it->second;
}

inline double celsius(double kelvin) { return kelvin - 273.15; }

// One backward-Euler step over dt. For EVERY branch in `solution`, look up the
// component type in `circuit` (circuit.findComponent(branch.componentId)),
// compute P_diss = dissipatedPowerOnly(type, branch.power), and integrate that
// component's temperature. Components not yet present start at kAmbientTemperature.
// Advances state.time by dt. If dt <= 0, do nothing.
inline void stepThermal(ThermalState& state, const Circuit& circuit,
                        const CircuitSolution& solution, double dt) {
    if (dt <= 0.0)
        return;
    for (const auto& branch : solution.branches) {
        const auto* comp = circuit.findComponent(branch.componentId);
        if (!comp)
            continue;
        double Pdiss = dissipatedPowerOnly(comp->type, branch.power);
        double Told = temperatureFor(state, branch.componentId);
        double a = kThermalCapacitance / dt;
        double Tnew = (a * Told + Pdiss + kAmbientTemperature / kThermalResistance)
                      / (a + 1.0 / kThermalResistance);
        state.temperature[branch.componentId] = Tnew;
    }
    state.time += dt;
}

} // namespace current_lab::physics
