#pragma once

#include "circuit/Circuit.h"
#include "physics/ParticleSim.h"
#include "solver/CircuitSolver.h"
#include <algorithm>
#include <vector>

// Builds the microdynamics channels for a circuit + solution. Two worlds:
//   electrons (Physics view): no mechanical obstacles inside sources — the
//     EMF drives charges via the field, so the source channel is clear;
//   water (Hydraulic view): the pump impeller is a real paddle collider.
namespace current_lab::physics {

inline std::vector<ChannelSpec> makeChannelSpecs(const Circuit& circuit,
                                                 const CircuitSolution* solution,
                                                 double wireThickness,
                                                 bool waterWorld) {
    std::vector<ChannelSpec> specs;
    for (const auto& comp : circuit.components) {
        if (comp.type == ComponentType::Ground || comp.type == ComponentType::Capacitor)
            continue;
        if (comp.type == ComponentType::Switch && comp.value < 0.5)
            continue; // open: no flow path
        const Node* a = circuit.findNode(comp.nodeA);
        const Node* b = circuit.findNode(comp.nodeB);
        if (!a || !b) continue;

        double current = 0.0;
        if (solution) {
            for (const auto& br : solution->branches)
                if (br.componentId == comp.id) { current = br.current; break; }
        }

        ChannelSpec spec;
        spec.componentId = comp.id;
        spec.a = a->position;
        spec.b = b->position;
        spec.nodeA = comp.nodeA;
        spec.nodeB = comp.nodeB;
        spec.halfWidth = wireThickness * 0.5;
        // Mean drift calibrated to the solver current: sign exact, magnitude
        // monotone (clamped for visibility; stated in the status doc).
        // Electrons are negative carriers: they drift AGAINST conventional
        // current. Water flows WITH it (volume flow analogy).
        double drive = std::clamp(current * 4000.0, -120.0, 120.0);
        spec.targetSpeed = waterWorld ? drive : -drive;
        spec.scatterers = comp.type == ComponentType::Resistor;
        spec.paddle = waterWorld && comp.type == ComponentType::VoltageSource;
        spec.paddleSpeed = std::clamp(current * 400.0, -9.0, 9.0);
        specs.push_back(spec);
    }
    return specs;
}

} // namespace current_lab::physics
