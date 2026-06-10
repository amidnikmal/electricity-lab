#pragma once

#include <unordered_map>
#include <vector>
#include "circuit/Circuit.h"

struct SolutionPoint {
    int nodeId = -1;
    double potential = 0.0; // V
};

struct BranchResult {
    int componentId = -1;
    double current = 0.0;     // A (conventional, from nodeA to nodeB)
    double voltageDrop = 0.0; // V (V_nodeA - V_nodeB)
    double power = 0.0;       // W (dissipated if positive, supplied if negative for sources)
};

struct CircuitSolution {
    std::vector<SolutionPoint> nodePotentials;
    std::vector<BranchResult> branches;
};

// Time-domain integration method for reactive elements (companion models).
// BackwardEuler: 1st order, A-stable, never blows up on large dt.
// Trapezoidal: 2nd order, more accurate at the same dt.
enum class IntegrationMethod {
    BackwardEuler,
    Trapezoidal,
};

// Accumulated state of reactive elements between transient steps.
// capVoltage: Vc = V(nodeA) - V(nodeB); indCurrent: Il flowing nodeA -> nodeB.
struct TransientState {
    double time = 0.0;
    std::unordered_map<int, double> capVoltage;
    std::unordered_map<int, double> capCurrent; // last-step current (trapezoidal history)
    std::unordered_map<int, double> indCurrent;
    std::unordered_map<int, double> indVoltage; // last-step voltage (trapezoidal history)

    void reset() { *this = TransientState{}; }
};

class CircuitSolver {
public:
    // DC steady state: capacitor = open circuit (gmin leak), inductor = short.
    CircuitSolution solve(const Circuit& circuit);

    // One transient step of size dt: assembles MNA with companion conductances
    // and history sources, solves, then advances state (Vc, Il) and state.time.
    CircuitSolution stepTransient(const Circuit& circuit, TransientState& state, double dt,
                                  IntegrationMethod method = IntegrationMethod::BackwardEuler);

    // Operating point at the CURRENT state without advancing time: capacitors
    // are held at their stored Vc (stiff source), inductors at their stored Il
    // (current source). Gives the honest t=0+ picture after a reset.
    CircuitSolution solveTransientSnapshot(const Circuit& circuit, const TransientState& state);

private:
    // Per-component companion: branch current model i_ab = g * (Va - Vb) - ieq.
    struct Companion {
        double g = 0.0;
        double ieq = 0.0;
    };

    CircuitSolution solveWithCompanions(const Circuit& circuit,
                                        const std::unordered_map<int, Companion>& companions);

    // Wraps solveWithCompanions with the ideal-diode state iteration: each
    // diode is either conducting (short) or blocking (open); states are
    // flipped until self-consistent (classic PWL fixed point, max 24 passes).
    CircuitSolution solveIterative(const Circuit& circuit,
                                   std::unordered_map<int, Companion> companions);
};

namespace current_lab::physics {

inline double capacitorEnergy(double capacitance, double voltage) {
    return 0.5 * capacitance * voltage * voltage;
}

inline double inductorEnergy(double inductance, double current) {
    return 0.5 * inductance * current * current;
}

} // namespace current_lab::physics
