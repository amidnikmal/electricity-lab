#pragma once

#include "imgui.h"
#include "circuit/Circuit.h"
#include "physics/PhysicalUnits.h"
#include "solver/CircuitSolver.h"
#include "ui/CircuitCanvas.h"
#include "ui/DualViewState.h"
#include "ui/InspectorPanel.h"
#include "ui/LearningPanel.h"
#include "ui/PaneLayout.h"
#include <memory>
#include <unordered_map>

enum class SimulationMode {
    DcSteadyState,
    Transient,
};

class MainWindow {
public:
    MainWindow();
    void render();
    void runSolver();

private:
    void advanceTransient(float realDt);
    void stepTransientOnce();
    void resetTransient();
    void setupTestCircuit();
    void renderToolbar();
    void renderLog();
    void renderTopBar();
    void renderToolRail();
    void renderRightInspector(const DistributedWireParameters& params);
    void renderBottomAnalysis(const DistributedWireParameters& params);
    void renderDualCanvasArea(float width, float height);
    void configureCanvasForCircuitView(CircuitCanvas& canvas);
    void configureCanvasForPhysicsView(CircuitCanvas& canvas);
    void configureCanvasForSpintronicsView(CircuitCanvas& canvas);
    void configureCanvasForProjection(CircuitCanvas& canvas, int projection);
    CircuitCanvas& canvasForPane(int paneId);
    void wireCanvas(CircuitCanvas& canvas);
    void syncCamerasFrom(const CircuitCanvas& source);
    void openElementEditor(int componentId);
    void renderElementEditor(const DistributedWireParameters& params);
    void wireCallbacks();
    void onCircuitChanged();
    void mapDistributedSolution();
    void applyVisualizationPreset(int presetIndex);

    Circuit m_circuit;
    Circuit m_distributedCircuit;
    CircuitSolver m_solver;
    CircuitSolution m_solution;
    CircuitSolution m_distributedSolution;
    bool m_solved = false;

    SimulationMode m_simMode = SimulationMode::DcSteadyState;
    TransientState m_transientState;
    IntegrationMethod m_integrationMethod = IntegrationMethod::BackwardEuler;
    bool m_transientRunning = false;
    double m_transientDt = 1e-3;       // s per solver step
    float m_transientSpeed = 1.0f;     // simulated seconds per real second
    double m_transientAccumulator = 0.0;

    current_lab::ui::PaneLayoutTree m_paneTree;
    std::unordered_map<int, std::unique_ptr<CircuitCanvas>> m_paneCanvases;
    bool m_animationPaused = false;
    float m_animationSpeed = 1.0f;
    current_lab::ui::DualViewState m_dualView;
    current_lab::ui::ElementEditState m_elementEdit;
    InspectorPanel m_inspector;
    LearningPanel m_learningPanel;
    EditorMode m_mode = EditorMode::Select;

    int m_selNode = -1;
    int m_selComp = -1;

    float m_leftWidth = 76;
    float m_rightWidth = 320;
    float m_logHeight = 120;
    float m_bottomHeight = 118;
    bool m_showCurrent = true;
    bool m_electronFlow = false;
    bool m_showPotential = true;
    bool m_showDrift = true;
    bool m_showEField = true;
    bool m_showHeat = true;
    bool m_showPower = true;
    bool m_showMagnetic = false;
    bool m_showSurfaceCharge = true;
    bool m_debugMode = false;
    bool m_showCanvasReadouts = false;
    bool m_showDebugLog = false;
    bool m_showRightInspector = true;
    bool m_fitDualViewsRequested = false;
    float m_wireThickness = 8.0f;
    int m_visualPreset = 3; // Current / Drift: animated layers on by default
    int m_distributedSegments = current_lab::physics::kDefaultDistributedWireSegments;
    double m_wireResistancePerUnit = current_lab::physics::kDefaultWireResistancePerUnit;
};
