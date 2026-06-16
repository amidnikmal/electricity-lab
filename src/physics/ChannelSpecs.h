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

// --- shared capacitor-tank geometry (sim AND renderer use these) -------------
// Гидравлический конденсатор = герметичный бак с упругой мембраной. В водяном
// мире он — ЧАСТЬ той же общей сети: узкие подводящие лиды от каждого терминала
// питают широкую камеру, разделённую мембраной (вода через мембрану НЕ проходит).
// Формулы дублируют projection::capacitorGeometry, чтобы коллайдер сети и
// отрисованный бак совпадали (физика не зависит от слоя проекции).

inline double capacitorTankPlateHalf(double wireThickness) {
    return std::max(wireThickness * 1.6, 14.0);
}

inline double capacitorTankHalfAxis(double wireThickness, double len) {
    double gap = std::clamp(wireThickness * 1.2, 6.0, len * 0.4);
    double plateHalf = capacitorTankPlateHalf(wireThickness);
    return std::max(plateHalf * 0.9, gap * 0.5);
}

// Осевое смещение апекса мембраны от Vc (та же формула, что в emitTank):
// плоская при Vc=0, выгибается к терминалу B при Vc>0. tankHalfAxis — полупролёт
// бака вдоль оси, range — глобальный размах потенциалов цепи.
inline double capacitorMembraneBow(double vc, double range, double tankHalfAxis) {
    double disp = std::clamp(vc / std::max(range, 1e-9), -1.0, 1.0);
    return disp * tankHalfAxis * 0.82;
}

// Синтетические узлы мембранного бака: устья камеры (lead<->tank). База большая,
// чтобы не пересечься с реальными id узлов; по 2 на конденсатор (capId уникален).
inline constexpr int kCapTankNodeBase = 1 << 20;

inline std::vector<ChannelSpec> makeChannelSpecs(const Circuit& circuit,
                                                 const CircuitSolution* solution,
                                                 double wireThickness,
                                                 bool waterWorld) {
    std::vector<ChannelSpec> specs;

    // Глобальный размах потенциалов и потенциал узла — для выгиба мембраны.
    double vLo = 0.0, vHi = 0.0;
    bool haveV = false;
    if (solution) {
        for (const auto& sp : solution->nodePotentials) {
            if (!haveV) { vLo = vHi = sp.potential; haveV = true; }
            vLo = std::min(vLo, sp.potential);
            vHi = std::max(vHi, sp.potential);
        }
    }
    double vRange = haveV ? (vHi - vLo) : 1.0;
    auto nodeV = [&](int nodeId) -> double {
        if (solution)
            for (const auto& sp : solution->nodePotentials)
                if (sp.nodeId == nodeId) return sp.potential;
        return 0.0;
    };
    auto currentOf = [&](int compId) -> double {
        if (solution)
            for (const auto& br : solution->branches)
                if (br.componentId == compId) return br.current;
        return 0.0;
    };

    for (const auto& comp : circuit.components) {
        if (comp.type == ComponentType::Ground)
            continue;
        if (comp.type == ComponentType::Switch && comp.value < 0.5)
            continue; // open: no flow path
        const Node* a = circuit.findNode(comp.nodeA);
        const Node* b = circuit.findNode(comp.nodeB);
        if (!a || !b) continue;

        // Конденсатор: в водяном мире — три однородных канала (узкий лид →
        // широкий бак с мембраной → узкий лид), все с componentId=capId и
        // connected, подключённые к магистрали по реальным узлам терминалов.
        // Однородная ширина каждого канала сохраняет всю транзитную логику
        // ParticleSim. В электронном мире заряды через зазор не текут — пропуск.
        if (comp.type == ComponentType::Capacitor) {
            if (!waterWorld) continue;
            Vec2 pa = a->position, pb = b->position;
            Vec2 ab = pb - pa;
            double len = ab.length();
            if (len < 1.0) continue;
            Vec2 unit = ab / len;
            Vec2 mid = pa + ab * 0.5;
            double tankHalfAxis = capacitorTankHalfAxis(wireThickness, len);
            double plateHalf = capacitorTankPlateHalf(wireThickness);
            Vec2 mouthA = mid - unit * tankHalfAxis;
            Vec2 mouthB = mid + unit * tankHalfAxis;
            double current = currentOf(comp.id);
            double drive = std::clamp(current * 4000.0, -120.0, 120.0);
            double vc = nodeV(comp.nodeA) - nodeV(comp.nodeB);
            double bow = capacitorMembraneBow(vc, vRange, tankHalfAxis);
            int mNodeA = kCapTankNodeBase + comp.id * 2;
            int mNodeB = kCapTankNodeBase + comp.id * 2 + 1;

            ChannelSpec leadA;
            leadA.componentId = comp.id;
            leadA.a = pa; leadA.b = mouthA;
            leadA.nodeA = comp.nodeA; leadA.nodeB = mNodeA;
            leadA.halfWidth = wireThickness * 0.5;
            leadA.targetSpeed = drive;
            leadA.connected = true;
            specs.push_back(leadA);

            ChannelSpec tank;
            tank.componentId = comp.id;
            tank.a = mouthA; tank.b = mouthB;
            tank.nodeA = mNodeA; tank.nodeB = mNodeB;
            tank.halfWidth = plateHalf;
            tank.targetSpeed = drive;
            tank.connected = true;
            tank.membrane = true;
            tank.membraneBow = bow;
            specs.push_back(tank);

            ChannelSpec leadB;
            leadB.componentId = comp.id;
            leadB.a = mouthB; leadB.b = pb;
            leadB.nodeA = mNodeB; leadB.nodeB = comp.nodeB;
            leadB.halfWidth = wireThickness * 0.5;
            leadB.targetSpeed = drive;
            leadB.connected = true;
            specs.push_back(leadB);
            continue;
        }

        double current = currentOf(comp.id);

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
