#pragma once

#include "circuit/Circuit.h"
#include "math/Vec2.h"
#include "solver/CircuitSolver.h"
#include <memory>
#include <vector>

// Unified closed-loop water pipe for the Hydraulic projection.
//
// Unlike ParticleSim (per-component channels with fragile particle transfers),
// HydraulicSim builds ONE continuous closed pipe per circuit loop. All water
// particles belong to a single pool with fixed count — the pipe goes through
// every component in the loop, so particles cycle naturally through the
// resistor, source, and wires without any hand-off logic.
//
// The pipe follows the component geometry in the loop order determined by
// current-flow direction. Pump paddles are placed at voltage sources,
// constriction pillars at resistors. Junction nodes become circular buffer
// regions where the pipe widens.
namespace current_lab::physics {

struct HydraulicParticle {
    Vec2 pos;
    Vec2 vel;
};

struct HydraulicPaddle {
    double angle = 0.0;   // rad
    int componentId = -1;
    int loopIndex = 0;
};

class HydraulicSim {
public:
    HydraulicSim();
    ~HydraulicSim();
    HydraulicSim(const HydraulicSim&) = delete;
    HydraulicSim& operator=(const HydraulicSim&) = delete;

    // Rebuilds the entire pipe world from the circuit. Call when geometry
    // changes or solution is first computed.
    void configure(const Circuit& circuit, const CircuitSolution* solution,
                   double pipeRadius);

    // Updates paddle speeds and target flow. Cheap, call every frame.
    void setFlow(const Circuit& circuit, const CircuitSolution* solution);

    void step(double dt);

    const std::vector<HydraulicParticle>& particles() const { return m_particles; }
    const std::vector<HydraulicPaddle>& paddles() const { return m_paddles; }
    bool configured() const { return m_configured; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::vector<HydraulicParticle> m_particles;
    std::vector<HydraulicPaddle> m_paddles;
    bool m_configured = false;
    double m_accumulator = 0.0;
    uint64_t m_signature = 0;

    static uint64_t layoutSignature(const Circuit& circuit);
};

} // namespace current_lab::physics
