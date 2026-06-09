#pragma once

#include "imgui.h"
#include "circuit/Circuit.h"
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
    void wireCallbacks();
    void onCircuitChanged();
    void mapDistributedSolution();

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

    float m_leftWidth = 160;
    float m_rightWidth = 280;
    float m_logHeight = 120;
    bool m_showCurrent = true;
    bool m_electronFlow = false;
    bool m_showPotential = true;
    bool m_showDrift = true;
    bool m_showEField = true;
    bool m_showHeat = true;
    bool m_showPower = true;
    bool m_showMagnetic = false;
    bool m_showSurfaceCharge = true;
    float m_wireThickness = 8.0f;
};
