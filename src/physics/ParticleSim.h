#pragma once

#include "math/Vec2.h"
#include <cstdint>
#include <memory>
#include <vector>

// Box2D-backed particle microdynamics for the visual layer (Drude picture):
// elastic particles confined to per-component channels, dragged by a force
// toward the solver-calibrated drift speed, scattering off lattice pillars in
// resistors and getting shoved by pump/crank paddles.
//
// Honesty contract: the MEAN drift per channel is calibrated to the solver
// current (sign and monotonic magnitude); the collision micro-motion is a
// qualitative Drude visualization, stated in PHYSICS_VISUAL_LAYER_STATUS.md.
namespace current_lab::physics {

struct ChannelSpec {
    int componentId = -1;
    Vec2 a, b;               // channel axis in world units
    int nodeA = -1, nodeB = -1; // circuit nodes: particles transfer through them
    double halfWidth = 4.0;  // world units
    double targetSpeed = 0.0; // signed, world units / s (calibrated from I)
    bool scatterers = false;  // Drude lattice pillars (resistor body)
    bool paddle = false;      // rotating impeller (pump) inside the channel
    double paddleSpeed = 0.0; // rad/s, signed (∝ flow)
    int seedParticles = -1;   // -1 = auto count; 0 = start empty (tests)
    // Water-network mode: walls are trimmed at shared nodes and junction
    // chambers connect the pipes, so particles flow through the WHOLE circuit
    // physically (no teleporting); packing is dense and the per-channel drive
    // becomes a weak assist — the pump impeller does the actual pushing.
    bool connected = false;
};

struct SimParticle {
    Vec2 pos;
    Vec2 vel;
    int componentId = -1;
};

// Real impeller angle from the physics world, so the drawn pump blades are
// exactly the colliders the particles bounce off.
struct PaddleState {
    int componentId = -1;
    double angle = 0.0; // radians
};

class ParticleSim {
public:
    ParticleSim();
    ~ParticleSim();
    ParticleSim(const ParticleSim&) = delete;
    ParticleSim& operator=(const ParticleSim&) = delete;

    // Rebuilds the world (walls, pillars, paddles, particles) for a new
    // channel layout. Call when the circuit geometry changes.
    void configure(const std::vector<ChannelSpec>& channels, double particleRadius);

    // Updates per-channel target speeds (every frame; cheap).
    void setTargets(const std::vector<ChannelSpec>& channels);

    // Advances the microdynamics; dt is real seconds (internally substepped).
    void step(double dt);

    std::vector<SimParticle> particles() const;
    std::vector<PaddleState> paddles() const;
    bool configured() const;

    // Geometry signature to detect when configure() is needed.
    static uint64_t layoutSignature(const std::vector<ChannelSpec>& channels);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace current_lab::physics
