#include "solver/CircuitSolver.h"
#include "math/LinearSystem.h"

static constexpr double kWireConductance = 1e9;

CircuitSolution CircuitSolver::solve(const Circuit& circuit) {
    CircuitSolution result;

    int N = static_cast<int>(circuit.nodes.size());
    if (N == 0) return result;

    int numVsource = 0;
    for (const auto& c : circuit.components) {
        if (c.type == ComponentType::VoltageSource) ++numVsource;
    }

    int groundIdx = circuit.groundNodeId;
    if (groundIdx < 0 || groundIdx >= N) groundIdx = 0;

    int unknowns = N - 1 + numVsource;
    LinearSystem sys;
    sys.resize(unknowns);

    auto rowForNode = [&](int nodeId) -> int {
        if (nodeId == groundIdx) return -1;
        return (nodeId < groundIdx) ? nodeId : nodeId - 1;
    };

    int vsRowOffset = N - 1;
    int vsIdx = 0;

    for (const auto& comp : circuit.components) {
        int aId = comp.nodeA;
        int bId = comp.nodeB;
        int ra = rowForNode(aId);
        int rb = rowForNode(bId);

        if (comp.type == ComponentType::Resistor || comp.type == ComponentType::Wire) {
            double g = (comp.type == ComponentType::Wire) ? kWireConductance : (1.0 / comp.value);
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

    for (int i = 0; i < N; ++i) {
        double pot = 0.0;
        if (i == groundIdx) {
            pot = 0.0;
        } else {
            int ri = rowForNode(i);
            if (ri >= 0 && ri < static_cast<int>(x.size())) pot = x[ri];
        }
        result.nodePotentials.push_back({i, pot});
    }

    vsIdx = 0;
    for (const auto& comp : circuit.components) {
        if (comp.type == ComponentType::Ground) continue;

        double Va = 0.0, Vb = 0.0;
        for (const auto& np : result.nodePotentials) {
            if (np.nodeId == comp.nodeA) Va = np.potential;
            if (np.nodeId == comp.nodeB) Vb = np.potential;
        }

        double dV = Va - Vb;
        double I = 0.0;

            if (comp.type == ComponentType::VoltageSource) {
            int vsr = vsRowOffset + vsIdx;
            if (vsr < static_cast<int>(x.size())) I = x[vsr];
            ++vsIdx;
        } else if (comp.type == ComponentType::Resistor) {
            I = dV / comp.value;
        } else if (comp.type == ComponentType::Wire) {
            I = dV * kWireConductance;
        }

        double P = I * dV;
        result.branches.push_back({comp.id, I, dV, P});
    }

    return result;
}
