#pragma once

#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include <cmath>
#include <unordered_map>

// Accumulated flow integrals: angle sources for every spinning visual.
// theta = k * ∫I dt is CONTINUOUS even when the current changes every frame
// (cranking the dynamo), unlike the naive theta = t * omega(t) which makes
// wheels teleport. Component integrals are signed (∝ charge passed); node
// integrals use the mean |I| of touching branches (gear spin magnitude).
namespace current_lab::projection {

struct FlowIntegrals {
    std::unordered_map<int, double> component; // componentId -> ∫I dt (signed)
    std::unordered_map<int, double> node;      // nodeId -> ∫ mean|I| dt
};

inline void advanceFlowIntegrals(FlowIntegrals& integrals, const Circuit& circuit,
                                 const CircuitSolution* solution, double dt) {
    if (!solution || dt <= 0.0) return;

    for (const auto& br : solution->branches)
        integrals.component[br.componentId] += br.current * dt;

    for (const auto& node : circuit.nodes) {
        double sum = 0.0;
        int touching = 0;
        for (const auto& comp : circuit.components) {
            if (comp.type == ComponentType::Ground) continue;
            if (comp.nodeA != node.id && comp.nodeB != node.id) continue;
            for (const auto& br : solution->branches) {
                if (br.componentId == comp.id) {
                    sum += std::abs(br.current);
                    ++touching;
                    break;
                }
            }
        }
        if (touching > 0)
            integrals.node[node.id] += (sum / touching) * dt;
    }
}

inline double componentIntegral(const FlowIntegrals* integrals, int componentId) {
    if (!integrals) return 0.0;
    auto it = integrals->component.find(componentId);
    return it != integrals->component.end() ? it->second : 0.0;
}

inline double nodeIntegral(const FlowIntegrals* integrals, int nodeId) {
    if (!integrals) return 0.0;
    auto it = integrals->node.find(nodeId);
    return it != integrals->node.end() ? it->second : 0.0;
}

} // namespace current_lab::projection
