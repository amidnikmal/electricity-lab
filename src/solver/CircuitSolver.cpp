#include "solver/CircuitSolver.h"
#include "math/LinearSystem.h"
#include <cmath>
#include <unordered_map>

static constexpr double kWireConductance = 1e9;
static constexpr double kOpenConductance = 1e-12; // gmin leak for DC capacitors

static double conductanceFor(const Component& comp) {
    if (comp.type == ComponentType::Wire) return kWireConductance;
    if (std::abs(comp.value) <= 1e-12) return kWireConductance;
    return 1.0 / comp.value;
}

CircuitSolution CircuitSolver::solveWithCompanions(
    const Circuit& circuit, const std::unordered_map<int, Companion>& companions) {
    CircuitSolution result;

    int N = static_cast<int>(circuit.nodes.size());
    if (N == 0) return result;

    std::unordered_map<int, int> nodeOrder;
    nodeOrder.reserve(circuit.nodes.size());
    for (int i = 0; i < N; ++i)
        nodeOrder[circuit.nodes[i].id] = i;

    int numVsource = 0;
    for (const auto& c : circuit.components) {
        if (c.type == ComponentType::VoltageSource) ++numVsource;
    }

    int groundId = circuit.groundNodeId;
    if (!circuit.findNode(groundId))
        groundId = circuit.nodes.front().id;
    int groundOrder = nodeOrder[groundId];

    int unknowns = N - 1 + numVsource;
    LinearSystem sys;
    sys.resize(unknowns);

    auto rowForNode = [&](int nodeId) -> int {
        auto it = nodeOrder.find(nodeId);
        if (it == nodeOrder.end() || nodeId == groundId) return -1;
        int order = it->second;
        return (order < groundOrder) ? order : order - 1;
    };

    auto companionFor = [&](int componentId) -> const Companion* {
        auto it = companions.find(componentId);
        return it != companions.end() ? &it->second : nullptr;
    };

    auto stampConductance = [&](int ra, int rb, double g) {
        if (ra >= 0) { sys.A[ra][ra] += g; if (rb >= 0) sys.A[ra][rb] -= g; }
        if (rb >= 0) { sys.A[rb][rb] += g; if (ra >= 0) sys.A[rb][ra] -= g; }
    };

    int vsRowOffset = N - 1;
    int vsIdx = 0;

    for (const auto& comp : circuit.components) {
        int ra = rowForNode(comp.nodeA);
        int rb = rowForNode(comp.nodeB);

        if (comp.type == ComponentType::Resistor || comp.type == ComponentType::Wire) {
            stampConductance(ra, rb, conductanceFor(comp));
        } else if (comp.type == ComponentType::Switch) {
            stampConductance(ra, rb, comp.value >= 0.5 ? kWireConductance : kOpenConductance);
        } else if (comp.type == ComponentType::VoltageSource) {
            int vsr = vsRowOffset + vsIdx;
            if (ra >= 0) { sys.A[ra][vsr] += 1.0; sys.A[vsr][ra] += 1.0; }
            if (rb >= 0) { sys.A[rb][vsr] -= 1.0; sys.A[vsr][rb] -= 1.0; }
            sys.b[vsr] = comp.value;
            ++vsIdx;
        } else if (comp.type == ComponentType::Capacitor ||
                   comp.type == ComponentType::Inductor ||
                   comp.type == ComponentType::Diode) {
            // Companion: branch current i_ab = g*(Va-Vb) - ieq, i.e. conductance g
            // in parallel with a current source ieq injecting into node A.
            // For diodes the companion encodes the current conducting/blocking
            // state chosen by solveIterative.
            const Companion* cm = companionFor(comp.id);
            if (!cm) continue;
            stampConductance(ra, rb, cm->g);
            if (ra >= 0) sys.b[ra] += cm->ieq;
            if (rb >= 0) sys.b[rb] -= cm->ieq;
        } else if (comp.type == ComponentType::Ground) {
            // ground is handled as reference node
        }
    }

    auto solveResult = sys.solve();
    const auto& x = solveResult.x;
    std::unordered_map<int, double> potentials;
    potentials.reserve(circuit.nodes.size());

    for (const auto& node : circuit.nodes) {
        double pot = 0.0;
        if (node.id != groundId) {
            int ri = rowForNode(node.id);
            if (ri >= 0 && ri < static_cast<int>(x.size())) pot = x[ri];
        }
        result.nodePotentials.push_back({node.id, pot});
        potentials[node.id] = pot;
    }

    vsIdx = 0;
    for (const auto& comp : circuit.components) {
        if (comp.type == ComponentType::Ground) continue;

        double Va = potentials.count(comp.nodeA) ? potentials[comp.nodeA] : 0.0;
        double Vb = potentials.count(comp.nodeB) ? potentials[comp.nodeB] : 0.0;

        double dV = Va - Vb;
        double I = 0.0;

        if (comp.type == ComponentType::VoltageSource) {
            int vsr = vsRowOffset + vsIdx;
            if (vsr < static_cast<int>(x.size())) I = x[vsr];
            ++vsIdx;
        } else if (comp.type == ComponentType::Resistor || comp.type == ComponentType::Wire) {
            I = dV * conductanceFor(comp);
        } else if (comp.type == ComponentType::Switch) {
            I = dV * (comp.value >= 0.5 ? kWireConductance : kOpenConductance);
        } else if (comp.type == ComponentType::Capacitor ||
                   comp.type == ComponentType::Inductor ||
                   comp.type == ComponentType::Diode) {
            if (const Companion* cm = companionFor(comp.id))
                I = dV * cm->g - cm->ieq;
        }

        double P = I * dV;
        result.branches.push_back({comp.id, I, dV, P});
    }

    return result;
}

CircuitSolution CircuitSolver::solveIterative(const Circuit& circuit,
                                              std::unordered_map<int, Companion> companions) {
    std::unordered_map<int, bool> conducting; // diode state guesses
    bool hasDiodes = false;
    for (const auto& comp : circuit.components) {
        if (comp.type == ComponentType::Diode) {
            conducting[comp.id] = false; // start blocked
            hasDiodes = true;
        }
    }
    if (!hasDiodes)
        return solveWithCompanions(circuit, companions);

    CircuitSolution solution;
    for (int pass = 0; pass < 24; ++pass) {
        for (const auto& [id, on] : conducting)
            companions[id] = {on ? kWireConductance : kOpenConductance, 0.0};

        solution = solveWithCompanions(circuit, companions);

        bool consistent = true;
        for (const auto& br : solution.branches) {
            auto it = conducting.find(br.componentId);
            if (it == conducting.end()) continue;
            if (it->second) {
                // Conducting diode must carry forward (A->B) current.
                if (br.current < -1e-12) { it->second = false; consistent = false; }
            } else {
                // Blocking diode must stay reverse- or zero-biased.
                if (br.voltageDrop > 1e-9) { it->second = true; consistent = false; }
            }
        }
        if (consistent) break;
    }
    return solution;
}

CircuitSolution CircuitSolver::solve(const Circuit& circuit) {
    std::unordered_map<int, Companion> companions;
    for (const auto& comp : circuit.components) {
        if (comp.type == ComponentType::Capacitor)
            companions[comp.id] = {kOpenConductance, 0.0}; // open circuit in DC
        else if (comp.type == ComponentType::Inductor)
            companions[comp.id] = {kWireConductance, 0.0}; // short circuit in DC
    }
    return solveIterative(circuit, std::move(companions));
}

CircuitSolution CircuitSolver::stepTransient(const Circuit& circuit, TransientState& state,
                                             double dt, IntegrationMethod method) {
    std::unordered_map<int, Companion> companions;

    for (const auto& comp : circuit.components) {
        if (comp.type == ComponentType::Capacitor) {
            double C = std::max(comp.value, 1e-15);
            double vOld = state.capVoltage.count(comp.id) ? state.capVoltage[comp.id] : 0.0;
            // Trapezoidal needs a consistent current history; the very first
            // step of a component falls back to backward Euler (standard
            // self-starting scheme), which the circuit solve makes consistent.
            bool hasHistory = state.capCurrent.count(comp.id) > 0;
            if (method == IntegrationMethod::BackwardEuler || !hasHistory) {
                double g = C / dt;
                companions[comp.id] = {g, g * vOld};
            } else {
                double g = 2.0 * C / dt;
                companions[comp.id] = {g, g * vOld + state.capCurrent[comp.id]};
            }
        } else if (comp.type == ComponentType::Inductor) {
            double L = std::max(comp.value, 1e-15);
            double iOld = state.indCurrent.count(comp.id) ? state.indCurrent[comp.id] : 0.0;
            bool hasHistory = state.indVoltage.count(comp.id) > 0;
            if (method == IntegrationMethod::BackwardEuler || !hasHistory) {
                companions[comp.id] = {dt / L, -iOld};
            } else {
                double g = dt / (2.0 * L);
                companions[comp.id] = {g, -(iOld + g * state.indVoltage[comp.id])};
            }
        }
    }

    CircuitSolution solution = solveIterative(circuit, std::move(companions));

    for (const auto& comp : circuit.components) {
        if (comp.type != ComponentType::Capacitor && comp.type != ComponentType::Inductor)
            continue;
        for (const auto& br : solution.branches) {
            if (br.componentId != comp.id) continue;
            if (comp.type == ComponentType::Capacitor) {
                state.capVoltage[comp.id] = br.voltageDrop;
                state.capCurrent[comp.id] = br.current;
            } else {
                state.indCurrent[comp.id] = br.current;
                state.indVoltage[comp.id] = br.voltageDrop;
            }
            break;
        }
    }

    state.time += dt;
    return solution;
}

CircuitSolution CircuitSolver::solveTransientSnapshot(const Circuit& circuit,
                                                      const TransientState& state) {
    std::unordered_map<int, Companion> companions;
    for (const auto& comp : circuit.components) {
        if (comp.type == ComponentType::Capacitor) {
            auto it = state.capVoltage.find(comp.id);
            double vc = it != state.capVoltage.end() ? it->second : 0.0;
            companions[comp.id] = {kWireConductance, kWireConductance * vc};
        } else if (comp.type == ComponentType::Inductor) {
            auto it = state.indCurrent.find(comp.id);
            double il = it != state.indCurrent.end() ? it->second : 0.0;
            companions[comp.id] = {kOpenConductance, -il};
        }
    }
    return solveIterative(circuit, std::move(companions));
}
