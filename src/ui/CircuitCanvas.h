#pragma once

#include "imgui.h"
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "math/Vec2.h"
#include "projection/ProjectionBuilder.h"
#include "ui/CanvasCamera.h"
#include "ui/CanvasInteraction.h"
#include "visualization/VisualizationPresets.h"
#include <algorithm>

// Thin canvas shell: owns camera + animation clock, feeds an input snapshot to
// CanvasInteraction, asks ProjectionBuilder for primitives and hands them to
// the renderer. It draws nothing component-specific itself.
class CircuitCanvas {
    current_lab::ui::CanvasInteraction m_interaction;

public:
    current_lab::ui::CanvasCallbacks& callbacks{m_interaction.callbacks};

    void render(Circuit& circuit, const CircuitSolution* solution);

    void setProjection(current_lab::projection::ProjectionKind kind) { m_projection = kind; }
    current_lab::projection::ProjectionKind projection() const { return m_projection; }

    void setMode(EditorMode m) { m_interaction.setMode(m); }
    EditorMode mode() const { return m_interaction.mode(); }
    void setSelected(int nodeId, int compId) { m_interaction.setSelected(nodeId, compId); }
    int selectedNode() const { return m_interaction.selectedNode(); }
    int selectedComponent() const { return m_interaction.selectedComponent(); }

    void setShowCurrent(bool v) { m_layers.current = v; }
    bool showCurrent() const { return m_layers.current; }
    void setElectronFlow(bool v) { m_layers.electronFlow = v; }
    bool electronFlow() const { return m_layers.electronFlow; }
    void setShowPotential(bool v) { m_layers.potential = v; }
    bool showPotential() const { return m_layers.potential; }
    void setShowDrift(bool v) { m_layers.drift = v; }
    bool showDrift() const { return m_layers.drift; }
    void setShowEField(bool v) { m_layers.electricField = v; }
    bool showEField() const { return m_layers.electricField; }
    void setShowHeat(bool v) { m_layers.heat = v; }
    bool showHeat() const { return m_layers.heat; }
    void setShowPower(bool v) { m_layers.power = v; }
    bool showPower() const { return m_layers.power; }
    void setShowMagnetic(bool v) { m_layers.magnetic = v; }
    bool showMagnetic() const { return m_layers.magnetic; }
    void setShowSurfaceCharge(bool v) { m_layers.surfaceCharge = v; }
    bool showSurfaceCharge() const { return m_layers.surfaceCharge; }
    void setShowCanvasReadouts(bool v) { m_layers.canvasReadouts = v; }
    bool showCanvasReadouts() const { return m_layers.canvasReadouts; }
    void setDebugView(bool v) { m_debugView = v; }
    bool debugView() const { return m_debugView; }
    void setReadOnly(bool v) { m_readOnly = v; }
    void setSimParticles(const std::vector<current_lab::physics::SimParticle>* particles) {
        m_simParticles = particles;
    }
    void setPaddleStates(const std::vector<current_lab::physics::PaddleState>* paddles) {
        m_paddleStates = paddles;
    }
    void setChainLinks(const std::vector<current_lab::physics::ChainLink>* links) {
        m_chainLinks = links;
    }
    void setFlowIntegrals(const current_lab::projection::FlowIntegrals* integrals) {
        m_flowIntegrals = integrals;
    }
    void setChainTravel(const std::unordered_map<int, double>* travel) {
        m_chainTravel = travel;
    }
    void setAxleCoupling(const current_lab::mechanics::AxleCoupling* coupling) {
        m_axleCoupling = coupling;
    }

    void setWireThickness(float v) { m_wireThickness = std::max(2.0f, std::min(50.0f, v)); }
    float wireThickness() const { return m_wireThickness; }
    void setUiScale(float s) { m_uiScale = s; }
    static float particleScreenRadius(float cameraScale) { return std::min(14.0f, 2.0f + 2.0f * cameraScale); }
    float wireScreenWidth() const { return m_wireThickness * m_camera.scale; }

    void setAnimationPaused(bool v) { m_animationPaused = v; }
    bool animationPaused() const { return m_animationPaused; }
    void setAnimationSpeed(float v) { m_animationSpeed = std::max(0.0f, v); }
    float animationSpeed() const { return m_animationSpeed; }
    void resetAnimationTime() { m_animationTime = 0.0; }
    double animationTime() const { return m_animationTime; }

    // test access
    int dragNode() const { return m_interaction.dragNode(); }
    int placeFromNode() const { return m_interaction.placeFromNode(); }
    void setDragNode(int id) { m_interaction.setDragNode(id); }
    void setPlaceFromNode(int id) { m_interaction.setPlaceFromNode(id); }
    CanvasCamera& camera() { return m_camera; }
    const CanvasCamera& camera() const { return m_camera; }
    void fitToCircuit(const Circuit& circuit);

    current_lab::projection::ViewParams makeViewParams() const;

private:
    ImVec2 toScreen(Vec2 w) const {
        ImVec2 ws = m_camera.worldToScreen(w);
        return ImVec2(ws.x + m_origin.x, ws.y + m_origin.y);
    }
    Vec2 toWorld(ImVec2 s) const {
        return m_camera.screenToWorld(ImVec2(s.x - m_origin.x, s.y - m_origin.y));
    }

    static current_lab::visualization::LayerVisibility defaultLayers() {
        current_lab::visualization::LayerVisibility layers;
        layers.current = true;
        layers.potential = true;
        layers.drift = true;
        layers.electricField = true;
        layers.heat = true;
        layers.power = true;
        layers.surfaceCharge = true;
        return layers;
    }

    CanvasCamera m_camera;
    current_lab::projection::ProjectionKind m_projection =
        current_lab::projection::ProjectionKind::Physics;
    current_lab::visualization::LayerVisibility m_layers = defaultLayers();

    ImVec2 m_origin{};
    ImVec2 m_size{};
    bool m_debugView = false;
    float m_wireThickness = 8.0f;
    bool m_readOnly = false;
    float m_uiScale = 1.0f;
    const std::vector<current_lab::physics::SimParticle>* m_simParticles = nullptr;
    const std::vector<current_lab::physics::PaddleState>* m_paddleStates = nullptr;
    const std::vector<current_lab::physics::ChainLink>* m_chainLinks = nullptr;
    const current_lab::projection::FlowIntegrals* m_flowIntegrals = nullptr;
    const std::unordered_map<int, double>* m_chainTravel = nullptr;
    const current_lab::mechanics::AxleCoupling* m_axleCoupling = nullptr;
    bool m_animationPaused = false;
    float m_animationSpeed = 1.0f;
    double m_animationTime = 0.0;

    // Hand-crank state (Mechanics view): drag around a drive wheel to generate EMF.
    int m_crankComponent = -1;
    double m_crankLastAngle = 0.0;
};
