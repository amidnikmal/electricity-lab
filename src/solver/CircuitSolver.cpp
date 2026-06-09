#include "solver/CircuitSolver.h"
#include "math/LinearSystem.h"
#include <cmath>
#include <unordered_map>

static constexpr double kWireConductance = 1e9;

static double conductanceFor(const Component& comp) {
    if (comp.type == ComponentType::Wire) return kWireConductance;
    if (std::abs(comp.value) <= 1e-12) return kWireConductance;
    return 1.0 / comp.value;
}

CircuitSolution CircuitSolver::solve(const Circuit& circuit) {
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

    int vsRowOffset = N - 1;
    int vsIdx = 0;

    for (const auto& comp : circuit.components) {
        int aId = comp.nodeA;
        int bId = comp.nodeB;
        int ra = rowForNode(aId);
        int rb = rowForNode(bId);

        if (comp.type == ComponentType::Resistor || comp.type == ComponentType::Wire) {
            double g = conductanceFor(comp);
            if (ra >= 0) { sys.A[ra][ra] += g; if (rb >= 0) sys.A[ra][rb] -= g; }
            if (rb >= 0) { sys.A[rb][rb] += g; if (ra >= 0) sys.A[rb][ra] -= g; }
        } else if (comp.type == ComponentType::VoltageSource) {
            int vsr = vsRowOffset + vsIdx;
            if (ra >= 0) { sys.A[ra][vsr] += 1.0; sys.A[vsr][ra] += 1.0; }
            if (rb >= 0) { sys.A[rb][vsr] -= 1.0; sys.A[vsr][rb] -= 1.0; }
            sys.b[vsr] = comp.value;
            ++vsIdx;
        } else if (comp.type == ComponentType::Ground) {
            // ground is handled as reference node
        }
    }

    auto x = sys.solve();
    std::unordered_map<int, double> potentials;
    potentials.reserve(circuit.nodes.size());

    for (const auto& node : circuit.nodes) {
        double pot = 0.0;
        if (node.id == groundId) {
            pot = 0.0;
        } else {
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
        } else if (comp.type == ComponentType::Resistor) {
            double g = conductanceFor(comp);
            I = dV * g;
        } else if (comp.type == ComponentType::Wire) {
            I = dV * kWireConductance;
        }

        double P = I * dV;
        result.branches.push_back({comp.id, I, dV, P});
    }

    return result;
}
