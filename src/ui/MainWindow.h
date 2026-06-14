#pragma once

#include "imgui.h"
#include "circuit/Circuit.h"
#include "physics/PhysicalUnits.h"
#include "simulation/LiveSim.h"
#include "solver/CircuitSolver.h"
#include "ui/CircuitCanvas.h"
#include "ui/DualViewState.h"
#include "ui/InspectorPanel.h"
#include "ui/LearningPanel.h"
#include "ui/PaneLayout.h"
#include "physics/ParticleSim.h"
#include "physics/ThermalModel.h"
#include "simulation/SignalRecorder.h"
#include <memory>
#include <unordered_map>

class MainWindow {
public:
    MainWindow();
    void render();
    void runSolver();

private:
    void advanceLiveSim(float realDt);
    void stepLiveSimOnce();
    void circuitEvent();        // правка/щелчок/ручка: будит LiveSim, заряд сохраняется
    void rebuildDistributed();
    void refreshSolution();
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
    void configureCanvasForMechanicsView(CircuitCanvas& canvas);
    void configureCanvasForProjection(CircuitCanvas& canvas, int projection);
    void updateParticleSim(float realDt);
    CircuitCanvas& canvasForPane(int paneId);
    void wireCanvas(CircuitCanvas& canvas);
    void syncCamerasFrom(const CircuitCanvas& source);
    void openElementEditor(int componentId);
    void renderElementEditor(const DistributedWireParameters& params);
    void wireCallbacks();
    void onCircuitChanged();
    void mapDistributedSolution();
    void applyVisualizationPreset(int presetIndex);
    double elementTemperatureK(int originalComponentId) const;

    Circuit m_circuit;
    Circuit m_distributedCircuit;
    CircuitSolver m_solver;
    CircuitSolution m_solution;
    CircuitSolution m_distributedSolution;
    bool m_solved = false;

    // Единый живой режим: DC steady и Transient слиты (см. simulation/LiveSim.h).
    current_lab::simulation::LiveSim m_liveSim;
    float m_manualSimSpeed = 1.0f; // слайдер ручной скорости (когда авто выключено)
    current_lab::physics::ThermalState m_thermal;
    current_lab::simulation::SignalRecorder m_recorder;

    current_lab::ui::PaneLayoutTree m_paneTree;
    // Two separate microdynamics worlds: electrons must not collide with the
    // water pump's impeller (no mechanical obstacles inside an EMF source).
    current_lab::physics::ParticleSim m_electronSim;
    current_lab::physics::ParticleSim m_waterSim;
    current_lab::physics::ChainSim m_chainSim;
    std::vector<current_lab::physics::ChainLink> m_chainLinks;
    current_lab::projection::FlowIntegrals m_flowIntegrals;
    // Honest chain travel per component (∫ targetSpeed dt) — the same quantity
    // that moves the sim rollers, so the drive sprocket/junction gears spin
    // WITH the chain instead of crawling on the ∫I dt phase. See ProjectionBuilder.
    std::unordered_map<int, double> m_chainTravel;
    std::vector<current_lab::physics::SimParticle> m_electronParticles;
    std::vector<current_lab::physics::SimParticle> m_waterParticles;
    std::vector<current_lab::physics::PaddleState> m_waterPaddles;
    int m_crankSavedComponent = -1;
    double m_crankSavedValue = 0.0;
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
    // Сглаженные тайминги кадра (EMA): внешние профилировщики в типичной
    // системе заблокированы, поэтому цифры всегда видны в нижней полосе.
    double m_perfSimMs = 0.0;   // updateParticleSim (все Box2D-миры)
    double m_perfPanesMs = 0.0; // renderDualCanvasArea (проекции + канвасы)
    int m_visualPreset = 3; // Current / Drift: animated layers on by default
    int m_distributedSegments = current_lab::physics::kDefaultDistributedWireSegments;
    double m_wireResistancePerUnit = current_lab::physics::kDefaultWireResistancePerUnit;
};
