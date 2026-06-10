#pragma once

#include "imgui.h"
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "math/Vec2.h"
#include <functional>

struct CanvasCamera {
    Vec2 offset{0.0f, 0.0f};
    float scale = 1.0f;

    ImVec2 worldToScreen(Vec2 world) const {
        return ImVec2(float(world.x * scale + offset.x), float(world.y * scale + offset.y));
    }

    Vec2 screenToWorld(ImVec2 screen) const {
        return Vec2((screen.x - offset.x) / scale, (screen.y - offset.y) / scale);
    }

    void pan(Vec2 delta) { offset = offset + delta; }

    void zoomAt(float factor, Vec2 screenPt) {
        float old = scale;
        scale *= factor;
        if (scale < 0.05f) scale = 0.05f;
        if (scale > 50.0f) scale = 50.0f;
        offset.x = screenPt.x - (scale / old) * (screenPt.x - offset.x);
        offset.y = screenPt.y - (scale / old) * (screenPt.y - offset.y);
    }
};

struct CanvasCallbacks {
    std::function<void(Vec2)> placeNode;
    std::function<void(int, int, ComponentType, double)> createComponent; // fromId, toId, type, value
    std::function<void(int)> selectNode;
    std::function<void(int)> selectComponent;
    std::function<void()> deselect;
    std::function<void(int, Vec2)> moveNode;
    std::function<void()> deleteSelected;
};

class CircuitCanvas {
public:
    void render(Circuit& circuit, const CircuitSolution* solution);

    CanvasCallbacks callbacks;
    void setMode(EditorMode m) {
        if (m_mode != m) {
            m_mode = m;
            m_dragNode = -1;
            m_placeFromNode = -1;
        }
    }
    EditorMode mode() const { return m_mode; }
    void setSelected(int nodeId, int compId) { m_selNode = nodeId; m_selComp = compId; }
    int selectedNode() const { return m_selNode; }
    int selectedComponent() const { return m_selComp; }
    void setShowCurrent(bool v) { m_showCurrent = v; }
    void setElectronFlow(bool v) { m_electronFlow = v; }
    bool showCurrent() const { return m_showCurrent; }
    bool electronFlow() const { return m_electronFlow; }
    void setShowPotential(bool v) { m_showPotential = v; }
    bool showPotential() const { return m_showPotential; }
    void setReadOnly(bool v) { m_readOnly = v; }
    void setShowDrift(bool v) { m_showDrift = v; }
    bool showDrift() const { return m_showDrift; }
    void setShowEField(bool v) { m_showEField = v; }
    bool showEField() const { return m_showEField; }
    void setShowHeat(bool v) { m_showHeat = v; }
    bool showHeat() const { return m_showHeat; }
    void setShowPower(bool v) { m_showPower = v; }
    bool showPower() const { return m_showPower; }
    void setShowMagnetic(bool v) { m_showMagnetic = v; }
    bool showMagnetic() const { return m_showMagnetic; }
    void setShowSurfaceCharge(bool v) { m_showSurfaceCharge = v; }
    bool showSurfaceCharge() const { return m_showSurfaceCharge; }
    void setDebugView(bool v) { m_debugView = v; }
    bool debugView() const { return m_debugView; }
    void setShowCanvasReadouts(bool v) { m_showCanvasReadouts = v; }
    bool showCanvasReadouts() const { return m_showCanvasReadouts; }
    void setWireThickness(float v) { m_wireThickness = std::max(2.0f, std::min(50.0f, v)); }
    float wireThickness() const { return m_wireThickness; }
    static float particleScreenRadius(float cameraScale) { return std::min(14.0f, 2.0f + 2.0f * cameraScale); }
    float wireScreenWidth() const { return m_wireThickness * m_camera.scale; }
    void setAnimationPaused(bool v) { m_animationPaused = v; }
    bool animationPaused() const { return m_animationPaused; }
    void setAnimationSpeed(float v) { m_animationSpeed = std::max(0.0f, v); }
    float animationSpeed() const { return m_animationSpeed; }
    void resetAnimationTime() { m_animationTime = 0.0; }
    double animationTime() const { return m_animationTime; }

    // test access
    int dragNode() const { return m_dragNode; }
    int placeFromNode() const { return m_placeFromNode; }
    void setDragNode(int id) { m_dragNode = id; }
    void setPlaceFromNode(int id) { m_placeFromNode = id; }
    CanvasCamera& camera() { return m_camera; }
    const CanvasCamera& camera() const { return m_camera; }
    void fitToCircuit(const Circuit& circuit);

private:
    ImVec2 toScreen(Vec2 w) const {
        ImVec2 ws = m_camera.worldToScreen(w);
        return ImVec2(ws.x + m_origin.x, ws.y + m_origin.y);
    }

    Vec2 toWorld(ImVec2 s) const {
        return m_camera.screenToWorld(ImVec2(s.x - m_origin.x, s.y - m_origin.y));
    }

    int hitTestNode(const Circuit& circuit, Vec2 worldPos) const;
    int hitTestComponent(const Circuit& circuit, Vec2 worldPos) const;

    void handleSelectMode(const Circuit& circuit);
    void handlePlaceMode(Circuit& circuit);

    void drawGrid(ImDrawList* dl);
    void drawElectricFieldBackdrop(ImDrawList* dl, const Circuit& circuit, const CircuitSolution* solution,
                                   double vMin, double vMax);
    void drawNode(ImDrawList* dl, const Node& node, const CircuitSolution* solution);
    void drawComponent(ImDrawList* dl, const Component& comp, const Circuit& circuit, const CircuitSolution* solution,
                       double vMin, double vMax, double globalMaxI, double globalMaxE, double globalMaxP);
    void drawConductorJunctions(ImDrawList* dl, const Circuit& circuit, const CircuitSolution* solution,
                                double vMin, double vMax);
    void drawVoltageSource(ImDrawList* dl, Vec2 a, Vec2 b, double value, double va, double vb, double vMin, double vMax);
    void drawResistor(ImDrawList* dl, Vec2 a, Vec2 b, double value, double va, double vb, double vMin, double vMax, double power = 0.0, double maxP = 0.0);
    void drawWire(ImDrawList* dl, Vec2 a, Vec2 b, double va, double vb, double vMin, double vMax);
    void drawGround(ImDrawList* dl, Vec2 pos);
    void drawCurrentArrows(ImDrawList* dl, Vec2 a, Vec2 b, double current, double globalMaxI, double conductorHalfWidth = -1.0);
    void drawEFieldArrows(ImDrawList* dl, Vec2 a, Vec2 b, double va, double vb, double maxE, double conductorHalfWidth = -1.0);
    void drawArrowHead(ImDrawList* dl, Vec2 pos, Vec2 dir, float size, ImU32 color);
    void drawPotentialLegend(ImDrawList* dl, double vMin, double vMax);
    void drawDriftParticles(ImDrawList* dl, Vec2 a, Vec2 b, double current, int compId, double visualThickness = -1.0, double driftSpeedScale = 1.0);
    void drawMagneticField(ImDrawList* dl, Vec2 a, Vec2 b, double current);
    void drawSurfaceCharge(ImDrawList* dl, Vec2 a, Vec2 b, double va, double vb, double vMin, double vMax, double visualThickness = -1.0);

    CanvasCamera m_camera;
    EditorMode m_mode = EditorMode::Select;

    int m_selNode = -1;
    int m_selComp = -1;

    int m_dragNode = -1;
    int m_placeFromNode = -1;

    ImVec2 m_origin{};
    ImVec2 m_size{};
    bool m_showCurrent = true;
    bool m_electronFlow = false;
    bool m_showPotential = true;
    bool m_showDrift = true;
    bool m_showEField = true;
    bool m_showHeat = true;
    bool m_showPower = true;
    bool m_showMagnetic = false;
    bool m_showSurfaceCharge = true;
    bool m_debugView = false;
    bool m_showCanvasReadouts = false;
    float m_wireThickness = 8.0f;
    bool m_readOnly = false;
    bool m_animationPaused = false;
    float m_animationSpeed = 1.0f;
    double m_animationTime = 0.0;
};
