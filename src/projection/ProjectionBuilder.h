#pragma once

#include "circuit/Circuit.h"
#include "physics/ChainSim.h"
#include "physics/ParticleSim.h"
#include "projection/FlowIntegrator.h"
#include "projection/MechanicsCoupling.h"
#include "solver/CircuitSolver.h"
#include "render/RenderPrimitives.h"
#include "visualization/VisualizationPresets.h"
#include <unordered_map>
#include <vector>

// Projection layer: turns (CircuitModel + CircuitSolution) into RenderPrimitives.
// This is the ONLY producer of render data for every view. Pure logic, no ImGui.
// Invariant: one model element = one ComponentId = N projections built from the
// same model+solution. No per-projection element collections exist anywhere.
namespace current_lab::projection {

enum class ProjectionKind {
    Schematic,   // clean circuit symbols
    Physics,     // continuous-matter view: potential, fields, drift, heat
    Mechanical,  // mechanical analogy: chains, brakes, springs, flywheels
    Hydraulic,   // water analogy: pipes, pumps, tanks, turbines, valves
};

struct ViewParams {
    visualization::LayerVisibility layers; // which physics layers to emit
    double wireThickness = 8.0;            // world units
    double cameraScale = 1.0;              // LOD + constant-screen-size sizing
    double time = 0.0;                     // animation clock (visualization only)
    bool debugView = false;
    int selectedNode = -1;
    int selectedComponent = -1;
    // Visible world rect (for field streamline clipping). Generous defaults
    // keep pure-logic tests independent of any viewport.
    Vec2 viewMin{-1e6, -1e6};
    Vec2 viewMax{1e6, 1e6};
    // When set, drift/flow layers render these Box2D microdynamics particles
    // instead of the stateless phase animation.
    const std::vector<physics::SimParticle>* simParticles = nullptr;
    // Real impeller angles from the water world (pump blades = colliders).
    const std::vector<physics::PaddleState>* paddleStates = nullptr;
    // Box2D chain links for the Mechanics view (rigid-jointed loops).
    const std::vector<physics::ChainLink>* chainLinks = nullptr;
    // Continuous rotation phases: theta = k * ∫I dt (no teleporting wheels).
    const FlowIntegrals* flowIntegrals = nullptr;
    // Honest chain travel per component (∫ targetSpeed dt = the SAME quantity
    // that moves the sim rollers, ~100x faster than ∫I dt). Drive sprockets and
    // junction gears spin at this rate so the wheel body turns WITH the chain
    // (no slip) instead of crawling on the ∫I dt phase while the chain flies by.
    const std::unordered_map<int, double>* chainTravel = nullptr;
    // Rigid-axle rotation signs: one sense per connected mechanism so every chain
    // on a shared node turns the same way. When chainTravel/chainLinks are
    // plumbed they already carry these signs; this pointer drives the stateless
    // fallback animation (and tests) coherently too. See MechanicsCoupling.h.
    const mechanics::AxleCoupling* coupling = nullptr;
};

// Solved values attributed to a model element inside one projection build.
// This is the runtime descendant of the old test-only DualViewProjection.
struct ElementState {
    int componentId = -1;
    ComponentType type = ComponentType::Wire;
    double voltageA = 0.0;
    double voltageB = 0.0;
    double current = 0.0;
    double power = 0.0;
    double storedEnergy = 0.0; // J: ½CV² / ½LI² for C/L, else 0
};

struct ProjectionResult {
    render::RenderPrimitives prims;
    std::vector<ElementState> elements;
};

ProjectionResult buildProjection(ProjectionKind kind,
                                 const Circuit& circuit,
                                 const CircuitSolution* solution,
                                 const ViewParams& params);

bool projectionHasComponent(const ProjectionResult& result, int componentId);
const ElementState* projectionElement(const ProjectionResult& result, int componentId);

} // namespace current_lab::projection
