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

// --- shared water-plumbing geometry (sim AND renderer use these) -------------

// Junction chamber radius at a node where pipes meet: wide enough that the
// mouths of two perpendicular pipes fit on the chamber circle.
inline double junctionRadius(double halfWidth) { return halfWidth * 1.5; }

// Sluice-wheel pump: the axle sits ON the pipe wall, the lower half of the
// wheel is buried BEHIND the wall (the wall blocks the back-flow under the
// axle), the blades sweep the lower half of the pipe and ADD momentum.
// IMPORTANT: the corridor above the blade tips stays >= 1.4 particle
// diameters — a sealed wheel turns the pump into a PLUG and the loop water
// piles up at the intake (regression: «шарики собираются внизу, насос не
// проталкивает»).
inline Vec2 pumpImpellerCenter(Vec2 a, Vec2 b, double halfWidth) {
    Vec2 unit = (b - a).normalized();
    Vec2 perp(-unit.y, unit.x);
    return (a + b) * 0.5 + perp * (-halfWidth);
}

// 0.95: corridor over the tips = 1.64 ball diameters. At 1.05 (1.48 diam,
// single file) the wheel throat was the arch-limited bottleneck of the loop —
// the flow saturated and stopped scaling with current.
inline double pumpImpellerRadius(double halfWidth) { return halfWidth * 0.95; }

// Impeller angular velocity for a wanted flow along a->b. Tip velocity on the
// exposed (+perp) side of an offset wheel is omega * (-unit), so positive
// flow needs NEGATIVE omega. Sign verified by test_water_network.
// Cap 6.0: at omega 9 the blade chops through the dense pack and squeezes
// balls visibly into each other near the wheel (incompressibility test).
inline double pumpOmegaForFlow(double current) {
    return -std::clamp(current * 400.0, -6.0, 6.0);
}

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
        spec.paddleSpeed = pumpOmegaForFlow(current);
        spec.connected = waterWorld; // one plumbing network, pump-driven
        specs.push_back(spec);
    }
    return specs;
}

} // namespace current_lab::physics
