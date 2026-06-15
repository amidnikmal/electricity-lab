#pragma once

#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "physics/PhysicalUnits.h"
#include "physics/PowerModel.h"

#include <unordered_map>
#include <utility>

namespace current_lab::physics {

// Сосредоточенная (lumped) RC тепловая модель, DISPLAY-ONLY (без обратной связи R(T)).
// Зеркало TransientState: температура по componentId, продвигается обратным Эйлером
// с тем же dt, что и электрический transient.
//   C_th dT/dt = P_diss - (T - T_amb)/R_th
// Установившийся режим: T = T_amb + P_diss * R_th.
struct ThermalState {
    double time = 0.0;
    std::unordered_map<int, double> temperature; // K, по componentId
    void reset() { *this = ThermalState{}; }
};

// Конфигурация теплового шага: позволяет переопределить R_th и C_th
// для отдельных компонентов (по componentId). Пустые map —
// используются глобальные константы kThermalResistance/kThermalCapacitance.
struct ThermalConfig {
    std::unordered_map<int, double> rThOverride; // componentId -> R_th (K/W)
    std::unordered_map<int, double> cThOverride; // componentId -> C_th (J/K)
};

// Temperature of a component in Kelvin; kAmbientTemperature if absent.
inline double temperatureFor(const ThermalState& state, int componentId) {
    auto it = state.temperature.find(componentId);
    return it == state.temperature.end() ? kAmbientTemperature : it->second;
}

inline double celsius(double kelvin) { return kelvin - 273.15; }

// Агрегаторы диапазона температур сцены для тепловой карты рендера.
inline std::pair<double, double> thermalRange(const ThermalState& state) {
    if (state.temperature.empty())
        return {kAmbientTemperature, kAmbientTemperature};
    auto it = state.temperature.begin();
    double minT = it->second, maxT = it->second;
    for (++it; it != state.temperature.end(); ++it) {
        double T = it->second;
        if (T < minT) minT = T;
        if (T > maxT) maxT = T;
    }
    return {minT, maxT};
}

inline double temperatureFraction(const ThermalState& state, int componentId) {
    auto [minT, maxT] = thermalRange(state);
    double T = temperatureFor(state, componentId);
    if (maxT <= minT)
        return 0.0;
    return (T - minT) / (maxT - minT);
}

// Один шаг обратным Эйлером за dt. Для КАЖДОЙ ветви в `solution` ищет тип компонента
// в `circuit`, вычисляет P_diss = dissipatedPowerOnly, интегрирует температуру.
// Компоненты, отсутствующие в state, начинают с kAmbientTemperature.
// Продвигает state.time на dt. При dt <= 0 — бездействует.
inline void stepThermal(ThermalState& state, const Circuit& circuit,
                        const CircuitSolution& solution, double dt,
                        const ThermalConfig& config) {
    if (dt <= 0.0)
        return;
    for (const auto& branch : solution.branches) {
        const auto* comp = circuit.findComponent(branch.componentId);
        if (!comp)
            continue;
        double Pdiss = dissipatedPowerOnly(comp->type, branch.power);
        double Told = temperatureFor(state, branch.componentId);

        // Per-component override R_th, иначе глобальная константа
        double Rth = kThermalResistance;
        auto rit = config.rThOverride.find(branch.componentId);
        if (rit != config.rThOverride.end())
            Rth = rit->second;

        // Per-component override C_th, иначе глобальная константа
        double Cth = kThermalCapacitance;
        auto cit = config.cThOverride.find(branch.componentId);
        if (cit != config.cThOverride.end())
            Cth = cit->second;

        double a = Cth / dt;
        double Tnew = (a * Told + Pdiss + kAmbientTemperature / Rth)
                      / (a + 1.0 / Rth);
        state.temperature[branch.componentId] = Tnew;
    }
    state.time += dt;
}

// Версия без конфигурации — использует глобальные константы по умолчанию
inline void stepThermal(ThermalState& state, const Circuit& circuit,
                        const CircuitSolution& solution, double dt) {
    stepThermal(state, circuit, solution, dt, ThermalConfig{});
}

} // namespace current_lab::physics
