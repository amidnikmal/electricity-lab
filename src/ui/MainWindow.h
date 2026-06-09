#pragma once

#include "imgui.h"
#include "circuit/Circuit.h"
#include "physics/PhysicalUnits.h"
#include "solver/CircuitSolver.h"
#include "ui/CircuitCanvas.h"
#include "ui/InspectorPanel.h"

class MainWindow {
public:
    MainWindow();
    void render();
    void runSolver();

private:
    void setupTestCircuit();
    void renderToolbar();
    void renderLog();
    void renderTopBar();
    void renderToolRail();
    void renderRightInspector(const DistributedWireParameters& params);
    void renderBottomAnalysis(const DistributedWireParameters& params);
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

    CircuitCanvas m_canvas;
    InspectorPanel m_inspector;
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
    float m_wireThickness = 8.0f;
    int m_visualPreset = 0;
    int m_distributedSegments = current_lab::physics::kDefaultDistributedWireSegments;
    double m_wireResistancePerUnit = current_lab::physics::kDefaultWireResistancePerUnit;
};
