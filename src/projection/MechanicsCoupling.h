#pragma once

#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"

#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

// Rigid-axle coupling for the Mechanics view.
//
// Spintronics reference: a node is a single physical spindle. Every sprocket and
// every chain sitting on that spindle is bolted to ONE rigid axle, so they all
// turn together — same direction, no slip relative to the axle.
//
// In our oval-per-component representation each chain wraps both of its end
// nodes; for an uncrossed belt over two pulleys BOTH end sprockets necessarily
// turn the same way for a given chain-travel direction (see ChainSim::Oval — the
// arcs around a and b both advance clockwise for positive phase). Therefore a
// node shared by several components can stay single-valued ONLY if every
// component in a connected mechanism shares one rotation sign. We take that sign
// from the dominant drive (largest |current|, voltage sources preferred) and let
// each chain keep |current| as its speed magnitude. Reversing the source then
// reverses the whole machine — exactly like a real chained mechanism, instead of
// the old behaviour where each component span span its own oval from its
// arbitrary nodeA->nodeB orientation and adjacent gears fought each other.
namespace current_lab::mechanics {

struct AxleCoupling {
    std::unordered_map<int, int> componentSign; // componentId -> +1 / -1
    std::unordered_map<int, int> nodeSign;      // nodeId -> rotation sense +1 / -1

    int signFor(int componentId) const {
        auto it = componentSign.find(componentId);
        return it == componentSign.end() ? 1 : it->second;
    }
    int nodeSignFor(int nodeId) const {
        auto it = nodeSign.find(nodeId);
        return it == nodeSign.end() ? 1 : it->second;
    }
};

// Components that take part in the rigid mechanism (and so need one coherent
// rotation sign). The capacitor IS included: its lead chains run at the loop
// current and must turn in sync with the neighbours at the shared nodes — the
// spring only stores the twist. Only ground and an OPEN switch carry nothing.
inline bool carriesChain(const Component& c) {
    if (c.type == ComponentType::Ground) return false;
    if (c.type == ComponentType::Switch && c.value < 0.5) return false; // open gap
    return true;
}

inline AxleCoupling computeAxleCoupling(const Circuit& circuit,
                                        const CircuitSolution* solution) {
    AxleCoupling out;

    auto currentOf = [&](int compId) -> double {
        if (!solution) return 0.0;
        for (const auto& br : solution->branches)
            if (br.componentId == compId) return br.current;
        return 0.0;
    };

    // Union-find over node ids: each chain-carrying component fuses its two
    // nodes into one rigid mechanism.
    std::unordered_map<int, int> parent;
    auto find = [&](int x) {
        while (true) {
            auto it = parent.find(x);
            if (it == parent.end()) { parent[x] = x; return x; }
            if (it->second == x) return x;
            parent[x] = parent[it->second]; // path halving
            x = parent[x];
        }
    };
    auto unite = [&](int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    };

    for (const auto& c : circuit.components) {
        if (!carriesChain(c)) continue;
        find(c.nodeA);
        find(c.nodeB);
        unite(c.nodeA, c.nodeB);
    }

    // Per connected mechanism, pick the dominant drive: among voltage sources the
    // largest |current|, else the largest |current| component overall. Its branch
    // current sign (nodeA->nodeB) becomes the whole mechanism's rotation sign, so
    // reversing the battery reverses everything coherently.
    struct Dominant { double absI = -1.0; double signedI = 0.0; bool source = false; };
    std::unordered_map<int, Dominant> dom; // root -> dominant info

    for (const auto& c : circuit.components) {
        if (!carriesChain(c)) continue;
        int root = find(c.nodeA);
        double i = currentOf(c.id);
        bool isSource = c.type == ComponentType::VoltageSource;
        Dominant& d = dom[root];
        // Sources outrank passives; within a tier the largest |current| wins.
        bool better = (isSource && !d.source) ||
                      (isSource == d.source && std::abs(i) > d.absI);
        if (better) {
            d.absI = std::abs(i);
            d.signedI = i;
            d.source = isSource;
        }
    }

    auto rootSign = [&](int root) -> int {
        auto it = dom.find(root);
        if (it == dom.end()) return 1;
        return it->second.signedI >= 0.0 ? 1 : -1;
    };

    for (const auto& n : circuit.nodes) {
        auto it = parent.find(n.id);
        if (it == parent.end()) continue; // isolated node, no chain
        out.nodeSign[n.id] = rootSign(find(n.id));
    }
    for (const auto& c : circuit.components) {
        if (!carriesChain(c)) continue;
        out.componentSign[c.id] = rootSign(find(c.nodeA));
    }

    return out;
}

} // namespace current_lab::mechanics
