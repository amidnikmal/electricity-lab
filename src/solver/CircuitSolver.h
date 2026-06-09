#pragma once

#include <vector>
#include "circuit/Circuit.h"

struct SolutionPoint {
    int nodeId = -1;
    double potential = 0.0; // V
};

struct BranchResult {
    int componentId = -1;
    double current = 0.0;    // A (conventional, from nodeA to nodeB)
    double voltageDrop = 0.0; // V (V_nodeA - V_nodeB)
    double power = 0.0;       // W (dissipated if positive, supplied if negative for sources)
};

struct CircuitSolution {
    std::vector<SolutionPoint> nodePotentials;
    std::vector<BranchResult> branches;
};

class CircuitSolver {
public:
    CircuitSolution solve(const Circuit& circuit);
};
