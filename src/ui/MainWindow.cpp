#include "ui/MainWindow.h"
#include "ui/I18n.h"
#include "ui/UiHelpers.h"
#include "visualization/VisualizationStatus.h"
#include "visualization/VisualizationPresets.h"
#include "ui/Format.h"
#include "physics/PowerModel.h"
#include "physics/WirePhysics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

void renderLayerToggle(const char* label, bool* value, current_lab::visualization::VisualizationLayer layer) {
    ImGui::Checkbox(label, value);
    if (ImGui::IsItemHovered()) {
        auto info = current_lab::visualization::layerStatus(layer);
        ImGui::BeginTooltip();
        ImGui::Text("%s", info.name);
        ImGui::Separator();
        ImGui::Text("Status: %s", info.badge);
        ImGui::TextWrapped("%s", info.model);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", info.description);
        ImGui::EndTooltip();
    }
}


double potentialFor(const CircuitSolution* solution, int nodeId) {
    if (!solution) return 0.0;
    for (const auto& np : solution->nodePotentials) {
        if (np.nodeId == nodeId) return np.potential;
    }
    return 0.0;
}

const BranchResult* branchFor(const CircuitSolution* solution, int componentId) {
    if (!solution) return nullptr;
    for (const auto& br : solution->branches) {
        if (br.componentId == componentId) return &br;
    }
    return nullptr;
}

const char* componentTypeLabel(ComponentType type) {
    switch (type) {
        case ComponentType::Wire: return "Wire";
        case ComponentType::Resistor: return "Resistor";
        case ComponentType::VoltageSource: return "Voltage Source";
        case ComponentType::Ground: return "Ground";
        case ComponentType::Capacitor: return "Capacitor";
        case ComponentType::Inductor: return "Inductor";
        case ComponentType::Diode: return "Diode";
        case ComponentType::Switch: return "Switch";
    }
    return "?";
}

const char* layerOnOff(bool enabled) {
    return enabled ? "on" : "hidden";
}

void renderLayerState(const char* label, bool enabled, current_lab::visualization::VisualizationLayer layer) {
    auto info = current_lab::visualization::layerStatus(layer);
    ImGui::Text("%s:", label);
    ImGui::SameLine(142.0f);
    ImGui::TextColored(enabled ? ImVec4(0.62f, 0.86f, 0.76f, 1.0f) : ImVec4(0.52f, 0.55f, 0.58f, 1.0f), "%s", layerOnOff(enabled));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", info.badge);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", info.name);
        ImGui::Separator();
        ImGui::TextWrapped("%s", info.model);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", info.description);
        ImGui::EndTooltip();
    }
}

void renderMetric(const char* label, const char* value) {
    ImGui::TextDisabled("%s", label);
    ImGui::TextWrapped("%s", value);
}

} // namespace

using current_lab::i18n::tr;

MainWindow::MainWindow() {
    wireCallbacks();
    m_inspector.onChange = [this]() { onCircuitChanged(); };
    m_learningPanel.loadCircuitIntoSimulator = [this](const Circuit& circuit) {
        m_circuit = circuit;
        m_selNode = -1;
        m_selComp = -1;
        m_dualView.clearSelection();
        m_elementEdit.close();
        m_fitDualViewsRequested = true;
        onCircuitChanged();
    };
    applyVisualizationPreset(m_visualPreset);
    setupTestCircuit();
    runSolver();
}

void MainWindow::wireCallbacks() {}

void MainWindow::wireCanvas(CircuitCanvas& canvas) {
    {
        canvas.callbacks.placeNode = [this](Vec2 pos) {
            m_circuit.addNode(pos);
            onCircuitChanged();
        };
        canvas.callbacks.createComponent = [this](int from, int to, ComponentType type, double val) {
            int id = m_circuit.addComponent(type, from, to, val);
            if (type == ComponentType::Ground) m_circuit.groundNodeId = to;
            m_selNode = -1;
            m_selComp = id;
            m_dualView.select(current_lab::ui::DualViewPane::Circuit, id);
            openElementEditor(id);
            onCircuitChanged();
        };
        canvas.callbacks.selectNode = [this](int id) {
            m_selNode = id;
            m_selComp = -1;
            m_dualView.clearSelection();
            m_elementEdit.close();
        };
        canvas.callbacks.selectComponent = [this](int id) {
            m_selComp = id;
            m_selNode = -1;
            m_dualView.select(current_lab::ui::DualViewPane::Circuit, id);
            openElementEditor(id);
        };
        canvas.callbacks.deselect = [this]() {
            m_selNode = -1;
            m_selComp = -1;
            m_dualView.clearSelection();
            m_elementEdit.close();
        };
        canvas.callbacks.moveNode = [this](int id, Vec2 pos) {
            Node* n = m_circuit.findNode(id);
            if (n) { n->position = pos; onCircuitChanged(); }
        };
        canvas.callbacks.deleteSelected = [this]() {
            if (m_selComp >= 0) {
                m_circuit.removeComponent(m_selComp);
                m_selComp = -1;
                m_dualView.clearSelection();
                m_elementEdit.close();
                onCircuitChanged();
            } else if (m_selNode >= 0) {
                m_circuit.removeNode(m_selNode);
                m_selNode = -1;
                onCircuitChanged();
            }
        };
    }
}

CircuitCanvas& MainWindow::canvasForPane(int paneId) {
    auto it = m_paneCanvases.find(paneId);
    if (it == m_paneCanvases.end()) {
        auto canvas = std::make_unique<CircuitCanvas>();
        wireCanvas(*canvas);
        // A new pane inherits the view of an existing one (Blender-style).
        if (!m_paneCanvases.empty())
            canvas->camera() = m_paneCanvases.begin()->second->camera();
        it = m_paneCanvases.emplace(paneId, std::move(canvas)).first;
    }
    return *it->second;
}

void MainWindow::syncCamerasFrom(const CircuitCanvas& source) {
    for (auto& [id, canvas] : m_paneCanvases)
        if (canvas.get() != &source)
            canvas->camera() = source.camera();
}

void MainWindow::onCircuitChanged() {
    runSolver();
    m_inspector.log().addMessage("Circuit changed, re-solving...");
}

void MainWindow::setupTestCircuit() {
    m_circuit = Circuit{};
    int gnd = m_circuit.addNode(Vec2(200, 300), "GND");
    int n1  = m_circuit.addNode(Vec2(200, 150), "N1");
    int n2  = m_circuit.addNode(Vec2(450, 150), "N2");
    m_circuit.groundNodeId = gnd;
    m_circuit.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    m_circuit.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    m_circuit.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    m_circuit.addComponent(ComponentType::Wire, n2, gnd, 0.0);
}

void MainWindow::runSolver() {
    DistributedWireParameters params;
    params.segmentsPerWire = m_distributedSegments;
    params.resistancePerUnit = m_wireResistancePerUnit;
    m_distributedCircuit = m_circuit.toDistributed(params);
    if (m_simMode == SimulationMode::Transient)
        m_distributedSolution = m_solver.solveTransientSnapshot(m_distributedCircuit, m_transientState);
    else
        m_distributedSolution = m_solver.solve(m_distributedCircuit);
    mapDistributedSolution();
    m_solved = true;
}

void MainWindow::advanceTransient(float realDt) {
    if (m_simMode != SimulationMode::Transient || !m_transientRunning)
        return;

    m_transientAccumulator += static_cast<double>(realDt) * m_transientSpeed;
    int steps = static_cast<int>(m_transientAccumulator / m_transientDt);
    // Keep the UI responsive even with tiny dt; simulation then lags real time.
    const int kMaxStepsPerFrame = 2000;
    if (steps > kMaxStepsPerFrame) {
        steps = kMaxStepsPerFrame;
        m_transientAccumulator = 0.0;
    } else {
        m_transientAccumulator -= steps * m_transientDt;
    }

    for (int i = 0; i < steps; ++i)
        m_distributedSolution = m_solver.stepTransient(m_distributedCircuit, m_transientState,
                                                       m_transientDt, m_integrationMethod);
    if (steps > 0) {
        mapDistributedSolution();
        m_solved = true;
    }
}

void MainWindow::stepTransientOnce() {
    m_distributedSolution = m_solver.stepTransient(m_distributedCircuit, m_transientState,
                                                   m_transientDt, m_integrationMethod);
    mapDistributedSolution();
    m_solved = true;
}

void MainWindow::resetTransient() {
    m_transientState.reset();
    m_transientAccumulator = 0.0;
    m_transientRunning = false;
    runSolver(); // snapshot of the honest t=0 state (Vc=0, Il=0)
}

void MainWindow::mapDistributedSolution() {
    auto potentialForNode = [&](int nodeId) {
        for (const auto& np : m_distributedSolution.nodePotentials) {
            if (np.nodeId == nodeId) return np.potential;
        }
        return 0.0;
    };

    m_solution.nodePotentials.clear();
    for (const auto& node : m_circuit.nodes)
        m_solution.nodePotentials.push_back({node.id, potentialForNode(node.id)});

    m_solution.branches.clear();
    for (const auto& oc : m_circuit.components) {
        if (oc.type == ComponentType::Ground)
            continue;

        BranchResult br;
        br.componentId = oc.id;
        bool isWire = (oc.type == ComponentType::Wire);

        double totalCurrent = 0.0;
        double totalVdrop = 0.0;
        double totalPower = 0.0;
        int segCount = 0;

        for (int di = 0; di < (int)m_distributedSolution.branches.size(); ++di) {
            int srcIdx = di < (int)m_distributedCircuit.distributedSource.size()
                             ? m_distributedCircuit.distributedSource[di] : -1;
            if (srcIdx != oc.id) continue;

            const auto& db = m_distributedSolution.branches[di];
            if (isWire) {
                totalCurrent += db.current;
                totalVdrop += db.voltageDrop;
                totalPower += db.power;
                segCount++;
            } else {
                br.current = db.current;
                br.voltageDrop = db.voltageDrop;
                br.power = db.power;
                break;
            }
        }

        if (isWire) {
            br.current = segCount > 0 ? totalCurrent / segCount : 0.0;
            br.voltageDrop = totalVdrop;
            br.power = totalPower;
        }

        m_solution.branches.push_back(br);
    }
}

void MainWindow::applyVisualizationPreset(int presetIndex) {
    using current_lab::visualization::VisualizationPreset;
    using current_lab::visualization::presetInfo;

    if (presetIndex < 0 || presetIndex >= static_cast<int>(VisualizationPreset::Count))
        presetIndex = static_cast<int>(VisualizationPreset::Circuit);

    auto info = presetInfo(static_cast<VisualizationPreset>(presetIndex));
    const auto& layers = info.layers;
    m_visualPreset = presetIndex;
    m_showCurrent = layers.current;
    m_electronFlow = layers.electronFlow;
    m_showPotential = layers.potential;
    m_showDrift = layers.drift;
    m_showEField = layers.electricField;
    m_showHeat = layers.heat;
    m_showPower = layers.power;
    m_showMagnetic = layers.magnetic;
    m_showSurfaceCharge = layers.surfaceCharge;
    m_showCanvasReadouts = layers.canvasReadouts;
    m_debugMode = layers.debugMarkers;
    m_showDebugLog = layers.debugLog;
}

static const char* modeLabel(EditorMode m) {
    switch (m) {
        case EditorMode::Select:           return "Select";
        case EditorMode::PlaceNode:        return "Node";
        case EditorMode::PlaceWire:        return "Wire";
        case EditorMode::PlaceResistor:    return "Resistor";
        case EditorMode::PlaceVoltageSource: return "V Source";
        case EditorMode::PlaceGround:      return "Ground";
        case EditorMode::PlaceCapacitor:   return "Capacitor";
        case EditorMode::PlaceInductor:    return "Inductor";
        case EditorMode::PlaceDiode:       return "Diode";
        case EditorMode::PlaceSwitch:      return "Switch";
    }
    return "?";
}

void MainWindow::render() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 7));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(14, 17, 20, 255));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 24, 27, 245));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(34, 39, 43, 255));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(42, 57, 64, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(55, 75, 82, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(69, 95, 96, 255));

    ImGui::Begin("MainWindow", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings);

    advanceTransient(ImGui::GetIO().DeltaTime);

    DistributedWireParameters params;
    params.segmentsPerWire = m_distributedSegments;
    params.resistancePerUnit = m_wireResistancePerUnit;

    renderTopBar();

    float bottomHeight = m_debugMode && m_showDebugLog ? 232.0f : m_bottomHeight;
    float availY = std::max(220.0f, ImGui::GetContentRegionAvail().y - bottomHeight - 8.0f);

    // Rail width follows the widest (possibly translated) tool label.
    float railWidth = m_leftWidth;
    {
        const char* railLabels[] = {
            tr("Select"), tr("Node"), tr("Wire"), tr("Resistor"), tr("V Source"),
            tr("Ground"), tr("Capacitor"), tr("Inductor"), tr("Diode"), tr("Switch"),
            tr("Probe"), tr("Pan"),
        };
        for (const char* label : railLabels)
            railWidth = std::max(railWidth, ImGui::CalcTextSize(label).x + 28.0f);
    }
    ImGui::BeginChild("ToolRail", ImVec2(railWidth, availY), ImGuiChildFlags_Border);
    renderToolRail();
    ImGui::EndChild();

    ImGui::SameLine();

    float remainingX = ImGui::GetContentRegionAvail().x;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    auto layout = current_lab::ui::computeDualViewLayout(remainingX, m_rightWidth,
                                                         m_showRightInspector,
                                                         m_paneTree.paneCount() > 1, gap);
    renderDualCanvasArea(layout.canvasWidth, availY);

    if (layout.showInspector) {
        ImGui::SameLine();
        ImGui::BeginChild("RightInspector", ImVec2(layout.inspectorWidth, availY), ImGuiChildFlags_Border);
        renderRightInspector(params);
        ImGui::EndChild();
    } else if (m_showRightInspector) {
        ImGui::SameLine();
        ImGui::BeginChild("RightInspectorCollapsed", ImVec2(40, availY), ImGuiChildFlags_Border);
        ImGui::TextDisabled("Inspector");
        ImGui::TextDisabled("auto");
        ImGui::TextDisabled("hidden");
        ImGui::EndChild();
    }

    renderBottomAnalysis(params);
    renderElementEditor(params);
    m_learningPanel.render();

    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

void MainWindow::configureCanvasForCircuitView(CircuitCanvas& canvas) {
    canvas.setProjection(current_lab::projection::ProjectionKind::Schematic);
    canvas.setMode(m_mode);
    canvas.setSelected(m_selNode, m_selComp);
    canvas.setShowCurrent(false);
    canvas.setElectronFlow(false);
    canvas.setShowPotential(false);
    canvas.setShowDrift(false);
    canvas.setShowEField(false);
    canvas.setShowHeat(false);
    canvas.setShowPower(false);
    canvas.setShowMagnetic(false);
    canvas.setShowSurfaceCharge(false);
    canvas.setDebugView(m_debugMode);
    canvas.setShowCanvasReadouts(m_showCanvasReadouts);
    canvas.setWireThickness(m_wireThickness);
    canvas.setReadOnly(false);
    canvas.setAnimationPaused(m_animationPaused);
    canvas.setAnimationSpeed(m_animationSpeed);
}

void MainWindow::configureCanvasForPhysicsView(CircuitCanvas& canvas) {
    canvas.setProjection(current_lab::projection::ProjectionKind::Physics);
    canvas.setMode(m_mode);
    canvas.setSelected(m_selNode, m_selComp);
    canvas.setShowCurrent(m_showCurrent);
    canvas.setElectronFlow(m_electronFlow);
    canvas.setShowPotential(m_showPotential);
    canvas.setShowDrift(m_showDrift);
    canvas.setShowEField(m_showEField);
    canvas.setShowHeat(m_showHeat);
    canvas.setShowPower(m_showPower);
    canvas.setShowMagnetic(m_showMagnetic);
    canvas.setShowSurfaceCharge(m_showSurfaceCharge);
    canvas.setDebugView(m_debugMode);
    canvas.setShowCanvasReadouts(m_showCanvasReadouts);
    canvas.setWireThickness(m_wireThickness);
    canvas.setReadOnly(false);
    canvas.setAnimationPaused(m_animationPaused);
    canvas.setAnimationSpeed(m_animationSpeed);
}

void MainWindow::configureCanvasForSpintronicsView(CircuitCanvas& canvas) {
    configureCanvasForPhysicsView(canvas);
    canvas.setProjection(current_lab::projection::ProjectionKind::Spintronics);
}

void MainWindow::configureCanvasForProjection(CircuitCanvas& canvas, int projection) {
    auto kind = static_cast<current_lab::projection::ProjectionKind>(projection);
    if (kind == current_lab::projection::ProjectionKind::Schematic) {
        configureCanvasForCircuitView(canvas);
    } else if (kind == current_lab::projection::ProjectionKind::Spintronics) {
        configureCanvasForSpintronicsView(canvas);
    } else if (kind == current_lab::projection::ProjectionKind::Hydraulic) {
        configureCanvasForPhysicsView(canvas);
        canvas.setProjection(current_lab::projection::ProjectionKind::Hydraulic);
    } else {
        configureCanvasForPhysicsView(canvas);
    }
}

void MainWindow::renderDualCanvasArea(float width, float height) {
    const CircuitSolution* solution = m_solved ? &m_solution : nullptr;

    ImGui::BeginChild("PaneAreaContainer", ImVec2(width, height), 0,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    float innerWidth = ImGui::GetContentRegionAvail().x;
    float innerHeight = ImGui::GetContentRegionAvail().y;
    const float gap = 6.0f;

    std::vector<current_lab::ui::PaneLeafInfo> leaves;
    std::vector<current_lab::ui::PaneSplitterInfo> splitters;
    m_paneTree.layout({0.0f, 0.0f, innerWidth, innerHeight}, gap, leaves, splitters);

    // Drop canvases of panes that no longer exist.
    for (auto it = m_paneCanvases.begin(); it != m_paneCanvases.end();) {
        bool alive = false;
        for (const auto& leaf : leaves)
            alive = alive || leaf.paneId == it->first;
        it = alive ? std::next(it) : m_paneCanvases.erase(it);
    }

    int closeRequest = -1;
    int splitRequest = -1;
    bool splitSideBySide = true;

    const char* projections[] = {tr("Circuit"), tr("Physics"), tr("Spintronics"), tr("Water")};

    for (const auto& leaf : leaves) {
        CircuitCanvas& canvas = canvasForPane(leaf.paneId);
        configureCanvasForProjection(canvas, leaf.projection);

        ImGui::SetCursorScreenPos(ImVec2(origin.x + leaf.rect.x, origin.y + leaf.rect.y));
        ImGui::PushID(leaf.paneId);
        ImGui::BeginChild("Pane", ImVec2(leaf.rect.w, leaf.rect.h), ImGuiChildFlags_Border,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Pane header: projection selector + split / close controls.
        int projection = leaf.projection;
        ImGui::SetNextItemWidth(118.0f);
        if (ImGui::Combo("##proj", &projection, projections, IM_ARRAYSIZE(projections)))
            m_paneTree.setProjection(leaf.paneId, projection);
        current_lab::ui::tooltipIfTruncated(projections[projection], 118.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("| |")) { splitRequest = leaf.paneId; splitSideBySide = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Split left/right"));
        ImGui::SameLine();
        if (ImGui::SmallButton("=")) { splitRequest = leaf.paneId; splitSideBySide = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Split top/bottom"));
        if (m_paneTree.paneCount() > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                closeRequest = leaf.paneId;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Close view"));
        }

        CanvasCamera before = canvas.camera();
        canvas.render(m_circuit, solution);
        if (m_fitDualViewsRequested)
            canvas.fitToCircuit(m_circuit);
        ImGui::EndChild();
        ImGui::PopID();

        if (m_dualView.syncCameras &&
            !current_lab::ui::cameraApproximatelyEqual(before, canvas.camera()))
            syncCamerasFrom(canvas);
    }

    // Draggable dividers between sibling panes.
    int splitterIndex = 0;
    for (const auto& divider : splitters) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + divider.rect.x, origin.y + divider.rect.y));
        ImGui::PushID(10000 + splitterIndex++);
        ImGui::InvisibleButton("##divider",
                               ImVec2(std::max(4.0f, divider.rect.w), std::max(4.0f, divider.rect.h)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(divider.sideBySide ? ImGuiMouseCursor_ResizeEW
                                                     : ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive() && divider.node) {
            float delta = divider.sideBySide ? ImGui::GetIO().MouseDelta.x
                                             : ImGui::GetIO().MouseDelta.y;
            divider.node->ratio = std::clamp(
                divider.node->ratio + delta / std::max(1.0f, divider.axisExtent),
                current_lab::ui::kPaneMinRatio, 1.0f - current_lab::ui::kPaneMinRatio);
        }
        ImGui::PopID();
    }

    // Apply structural edits after the layout pass.
    if (splitRequest >= 0) {
        int newId = m_paneTree.split(splitRequest, splitSideBySide);
        if (newId >= 0) {
            CircuitCanvas& fresh = canvasForPane(newId);
            fresh.camera() = canvasForPane(splitRequest).camera();
        }
    }
    if (closeRequest >= 0) {
        if (m_paneTree.close(closeRequest))
            m_paneCanvases.erase(closeRequest);
    }

    m_fitDualViewsRequested = false;
    ImGui::EndChild();
}

void MainWindow::openElementEditor(int componentId) {
    const Component* component = m_circuit.findComponent(componentId);
    if (!component)
        return;

    m_elementEdit.open(componentId, component->value, m_wireResistancePerUnit, m_distributedSegments);
}

void MainWindow::renderTopBar() {
    const char* presetLabels[] = {
        tr("Circuit"), tr("Potential"), tr("Electric Field"), tr("Current / Drift"),
        tr("Power / Heat"), tr("Charges"), tr("Debug"),
    };

    ImGui::BeginChild("TopBar", ImVec2(0, 44), ImGuiChildFlags_Border);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Current Lab");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("Workspace"));

    ImGui::SameLine(210.0f);
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("##VisualizationPreset", &m_visualPreset, presetLabels, IM_ARRAYSIZE(presetLabels)))
        applyVisualizationPreset(m_visualPreset);
    current_lab::ui::tooltipIfTruncated(presetLabels[m_visualPreset], 150.0f);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(105.0f);
    int simMode = static_cast<int>(m_simMode);
    const char* simModes[] = {tr("DC steady"), tr("Transient")};
    bool simModeChanged = ImGui::Combo("##SimMode", &simMode, simModes, IM_ARRAYSIZE(simModes));
    current_lab::ui::tooltipIfTruncated(simModes[simMode], 105.0f);
    if (simModeChanged) {
        m_simMode = static_cast<SimulationMode>(simMode);
        if (m_simMode == SimulationMode::Transient) {
            resetTransient();
        } else {
            m_transientRunning = false;
            runSolver();
        }
    }

    if (m_simMode == SimulationMode::Transient) {
        ImGui::SameLine();
        if (ImGui::Button(m_transientRunning ? tr("Pause##sim") : tr("Run##sim")))
            m_transientRunning = !m_transientRunning;
        ImGui::SameLine();
        if (ImGui::Button(tr("Step")))
            stepTransientOnce();
        ImGui::SameLine();
        if (ImGui::Button(tr("Reset t")))
            resetTransient();
        ImGui::SameLine();
        ImGui::Text(tr("t = %.3f s"), m_transientState.time);
    } else {
        ImGui::SameLine();
        if (ImGui::Button(tr("Run Solver"))) {
            runSolver();
            m_inspector.log().addMessage("Solver run manually.");
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("1##layout")) m_paneTree.resetSingle(1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Single"));
    ImGui::SameLine();
    if (ImGui::Button("2##layout")) m_paneTree.resetDual();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Dual"));
    ImGui::SameLine();
    if (ImGui::Button("3##layout")) m_paneTree.resetTriple();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Triple"));

    ImGui::SameLine();
    ImGui::Checkbox(tr("Inspector"), &m_showRightInspector);

    ImGui::SameLine();
    ImGui::Checkbox(tr("Learn"), &m_learningPanel.open);

    ImGui::SameLine();
    if (ImGui::Button(tr("Fit")))
        m_fitDualViewsRequested = true;

    ImGui::SameLine();
    bool debug = m_debugMode;
    if (ImGui::Checkbox(tr("Debug"), &debug)) {
        applyVisualizationPreset(debug
            ? static_cast<int>(current_lab::visualization::VisualizationPreset::Debug)
            : static_cast<int>(current_lab::visualization::VisualizationPreset::Circuit));
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(58.0f);
    int lang = static_cast<int>(current_lab::i18n::language());
    if (ImGui::Combo("##Lang", &lang, "EN\0RU\0"))
        current_lab::i18n::setLanguage(static_cast<current_lab::i18n::Language>(lang));
    ImGui::EndChild();
}

void MainWindow::renderToolRail() {
    ImGui::TextDisabled("%s", tr("Tools"));
    ImGui::Separator();

    EditorMode modes[] = {
        EditorMode::Select,
        EditorMode::PlaceNode,
        EditorMode::PlaceWire,
        EditorMode::PlaceResistor,
        EditorMode::PlaceVoltageSource,
        EditorMode::PlaceGround,
        EditorMode::PlaceCapacitor,
        EditorMode::PlaceInductor,
        EditorMode::PlaceDiode,
        EditorMode::PlaceSwitch,
    };

    for (auto m : modes) {
        bool active = (m_mode == m);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(79, 101, 88, 255));
        if (ImGui::Button(tr(modeLabel(m)), ImVec2(-1, 30)))
            m_mode = m;
        if (active)
            ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("Read"));
    if (ImGui::Button(tr("Probe"), ImVec2(-1, 30)))
        m_mode = EditorMode::Select;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Readout follows the selected node or element in this pass.");
    if (ImGui::Button(tr("Pan"), ImVec2(-1, 30)))
        m_mode = EditorMode::Select;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Middle-drag pans the canvas; mouse wheel zooms.");
}

void MainWindow::renderRightInspector(const DistributedWireParameters& params) {
    using current_lab::visualization::VisualizationLayer;
    using current_lab::visualization::VisualizationPreset;
    using current_lab::visualization::presetInfo;

    auto preset = static_cast<VisualizationPreset>(m_visualPreset);
    auto info = presetInfo(preset);
    const CircuitSolution* solution = m_solved ? &m_solution : nullptr;
    const Component* selectedComp = m_circuit.findComponent(m_selComp);
    const Node* selectedNode = m_circuit.findNode(m_selNode);

    ImGui::SeparatorText(tr("Visualization"));
    ImGui::Text(tr("Mode: %s"), m_debugMode ? tr("Debug") : tr("Learner"));
    ImGui::Text(tr("Preset: %s"), tr(info.label));
    ImGui::TextWrapped("%s", info.modelNote);
    renderLayerState(tr("Potential"), m_showPotential, VisualizationLayer::Potential);
    renderLayerState(tr("E-field"), m_showEField, VisualizationLayer::ElectricField);
    renderLayerState(tr("Current"), m_showCurrent, VisualizationLayer::Current);
    renderLayerState(tr("Drift"), m_showDrift, VisualizationLayer::Drift);
    renderLayerState(tr("Heat"), m_showHeat, VisualizationLayer::Heat);
    renderLayerState(tr("Surface charge"), m_showSurfaceCharge, VisualizationLayer::SurfaceCharge);
    renderLayerState(tr("Magnetic"), m_showMagnetic, VisualizationLayer::MagneticField);

    if (m_debugMode && ImGui::CollapsingHeader("Raw Layer Switches", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderLayerToggle("Current", &m_showCurrent, VisualizationLayer::Current);
        renderLayerToggle("Electron flow", &m_electronFlow, VisualizationLayer::Drift);
        renderLayerToggle("Potential", &m_showPotential, VisualizationLayer::Potential);
        renderLayerToggle("Drift particles", &m_showDrift, VisualizationLayer::Drift);
        renderLayerToggle("E-field", &m_showEField, VisualizationLayer::ElectricField);
        renderLayerToggle("Heat", &m_showHeat, VisualizationLayer::Heat);
        renderLayerToggle("Power labels", &m_showPower, VisualizationLayer::Power);
        renderLayerToggle("Magnetic", &m_showMagnetic, VisualizationLayer::MagneticField);
        renderLayerToggle("Surface charge", &m_showSurfaceCharge, VisualizationLayer::SurfaceCharge);
    }

    ImGui::Spacing();
    ImGui::SeparatorText(tr("Probe Readout"));
    if (selectedComp) {
        const Node* a = m_circuit.findNode(selectedComp->nodeA);
        const Node* b = m_circuit.findNode(selectedComp->nodeB);
        double va = potentialFor(solution, selectedComp->nodeA);
        double vb = potentialFor(solution, selectedComp->nodeB);
        const BranchResult* br = branchFor(solution, selectedComp->id);
        double dV = br ? br->voltageDrop : (va - vb);
        double current = br ? br->current : 0.0;
        double power = br ? br->power : 0.0;
        double length = (a && b) ? (b->position - a->position).length() : 0.0;
        double e = length > 1e-9 ? std::abs(dV) / length : 0.0;
        ImGui::TextDisabled("%s", tr("Probe position"));
        ImGui::Text("%s", tr(componentTypeLabel(selectedComp->type)));
        ImGui::Text("V: %.4f -> %.4f %s", va, vb, tr("V"));
        ImGui::Text("E: %.5f %s/wu", e, tr("V"));
        ImGui::Text("I: %.4f %s", milliamps(current), tr("mA"));
        ImGui::Text("P: %.4f %s", milliwatts(current_lab::physics::dissipatedPowerOnly(selectedComp->type, power)), tr("mW"));
        ImGui::Text("Reference: node %d", m_circuit.groundNodeId);
    } else if (selectedNode) {
        double v = potentialFor(solution, selectedNode->id);
        ImGui::TextDisabled("Probe position");
        ImGui::Text("Selected node %d", selectedNode->id);
        ImGui::Text("V: %.4f V", v);
        ImGui::Text("E: n/a");
        ImGui::Text("I: n/a");
        ImGui::Text("P local: n/a");
        ImGui::Text("Reference: node %d", m_circuit.groundNodeId);
    } else {
        ImGui::TextDisabled("%s", tr("Select a node or component for numerical readout."));
    }

    ImGui::Spacing();
    ImGui::SeparatorText(tr("Selected Element"));
    if (selectedComp) {
        const Node* a = m_circuit.findNode(selectedComp->nodeA);
        const Node* b = m_circuit.findNode(selectedComp->nodeB);
        double va = potentialFor(solution, selectedComp->nodeA);
        double vb = potentialFor(solution, selectedComp->nodeB);
        const BranchResult* br = branchFor(solution, selectedComp->id);
        double dV = br ? br->voltageDrop : (va - vb);
        double current = br ? br->current : 0.0;
        double power = br ? br->power : 0.0;
        double length = (a && b) ? (b->position - a->position).length() : 0.0;
        double totalWireR = current_lab::physics::wireResistance(length, params.resistancePerUnit);
        double eSigned = length > 1e-9 ? dV / length : 0.0;

        ImGui::Text("Type: %s", tr(componentTypeLabel(selectedComp->type)));
        ImGui::Text("Name: %s %d", componentTypeLabel(selectedComp->type), selectedComp->id);
        ImGui::Text("Node A: %d", selectedComp->nodeA);
        ImGui::Text("Node B: %d", selectedComp->nodeB);
        ImGui::Text("Va: %.4f %s", va, tr("V"));
        ImGui::Text("Vb: %.4f %s", vb, tr("V"));
        ImGui::Text("dV: %.4f %s", dV, tr("V"));
        ImGui::Text("I: %.4f %s", milliamps(current), tr("mA"));
        if (selectedComp->type == ComponentType::Resistor)
            ImGui::Text("R: %.4f Ohm", selectedComp->value);
        if (selectedComp->type == ComponentType::VoltageSource)
            ImGui::Text("Source: %.4f V", selectedComp->value);
        if (selectedComp->type == ComponentType::Capacitor) {
            ImGui::Text("C: %.1f uF", selectedComp->value * 1e6);
            ImGui::Text("Vc: %.4f V", dV);
            ImGui::Text("E stored: %.4f mJ",
                        current_lab::physics::capacitorEnergy(selectedComp->value, dV) * 1000.0);
        }
        if (selectedComp->type == ComponentType::Inductor) {
            ImGui::Text("L: %.4f H", selectedComp->value);
            ImGui::Text("Il: %.4f mA", milliamps(current));
            ImGui::Text("E stored: %.4f mJ",
                        current_lab::physics::inductorEnergy(selectedComp->value, current) * 1000.0);
        }
        ImGui::Text("P: %.4f mW (%s)", milliwatts(power), current_lab::physics::isSupplyingPower(power) ? "supplied" : "dissipated");
        if (selectedComp->type == ComponentType::Wire) {
            ImGui::Text("Length: %.3f wu", length);
            ImGui::Text("R per unit: %.4f Ohm/wu", params.resistancePerUnit);
            ImGui::Text("Total R: %.4f Ohm", totalWireR);
            ImGui::Text("Segments: %d", params.segmentsPerWire);
            ImGui::Text("E ~= %.5f V/wu", eSigned);
        }
    } else if (selectedNode) {
        ImGui::Text("Type: Node");
        ImGui::Text("Name: %s", selectedNode->label.empty() ? "(unnamed)" : selectedNode->label.c_str());
        ImGui::Text("Node: %d", selectedNode->id);
        ImGui::Text("V: %.4f V", potentialFor(solution, selectedNode->id));
    } else {
        ImGui::TextDisabled("%s", tr("Nothing selected."));
    }

    ImGui::Spacing();
    ImGui::SeparatorText(tr("Simulation Controls"));
    if (m_simMode == SimulationMode::Transient) {
        ImGui::TextDisabled("%s", tr("Transient: companion-model MNA"));
        int method = static_cast<int>(m_integrationMethod);
        const char* methods[] = {tr("Backward Euler (stable)"), tr("Trapezoidal (accurate)")};
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo(tr("Method"), &method, methods, IM_ARRAYSIZE(methods)))
            m_integrationMethod = static_cast<IntegrationMethod>(method);
        double dtMs = m_transientDt * 1000.0;
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputDouble(tr("dt (ms)"), &dtMs, 0.1, 1.0, "%.3f"))
            m_transientDt = std::clamp(dtMs, 1e-3, 1000.0) / 1000.0;
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat(tr("Sim speed"), &m_transientSpeed, 0.05f, 20.0f, "%.2fx sim s / real s",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::Text("t = %.4f s", m_transientState.time);
    } else {
        if (ImGui::Button(tr("Run"), ImVec2(72, 0))) {
            runSolver();
            m_inspector.log().addMessage("Solver run manually.");
        }
    }
    ImGui::Checkbox(tr("Sync cameras"), &m_dualView.syncCameras);
    ImGui::Checkbox(tr("Pause animation"), &m_animationPaused);
    if (ImGui::Button(tr("Reset Time"))) {
        for (auto& [id, canvas] : m_paneCanvases)
            canvas->resetAnimationTime();
    }
    ImGui::SliderFloat(tr("Animation speed"), &m_animationSpeed, 0.0f, 4.0f, "%.2fx");
    ImGui::TextDisabled("%s", tr("Animation speed is visualization-only."));
    ImGui::SliderFloat(tr("Wire width"), &m_wireThickness, 2.0f, 50.0f, "%.1f wu");
    if (ImGui::SliderInt(tr("Wire segments"), &m_distributedSegments, 1, 32))
        onCircuitChanged();
    if (ImGui::InputDouble("R / unit", &m_wireResistancePerUnit, 0.05, 0.5, "%.3f")) {
        if (m_wireResistancePerUnit < 0.0)
            m_wireResistancePerUnit = 0.0;
        onCircuitChanged();
    }
    bool debug = m_debugMode;
    if (ImGui::Checkbox(tr("Debug mode"), &debug)) {
        applyVisualizationPreset(debug
            ? static_cast<int>(VisualizationPreset::Debug)
            : static_cast<int>(VisualizationPreset::Circuit));
    }
    if (ImGui::Button(tr("Clear"), ImVec2(72, 0))) {
        m_circuit = Circuit{};
        m_distributedCircuit = Circuit{};
        m_solution = CircuitSolution{};
        m_distributedSolution = CircuitSolution{};
        m_selNode = -1;
        m_selComp = -1;
        m_solved = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset Demo"))) {
        setupTestCircuit();
        m_selNode = -1;
        m_selComp = -1;
        runSolver();
    }

    if (m_debugMode && ImGui::CollapsingHeader("Verbose Inspector")) {
        m_inspector.render(m_circuit, solution, m_selNode, m_selComp, params,
                           m_wireThickness, m_animationSpeed, m_electronFlow);
    }
}


void MainWindow::renderElementEditor(const DistributedWireParameters& params) {
    if (!m_elementEdit.isOpen)
        return;

    Component* component = m_circuit.findComponent(m_elementEdit.componentId);
    if (!component) {
        m_elementEdit.close();
        return;
    }

    // Plain (non-modal) window: the rest of the UI must stay clickable while
    // an element is selected.
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
    bool keepOpen = true;
    if (!ImGui::Begin(tr("Element Editor"), &keepOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!keepOpen) {
        m_elementEdit.close();
        ImGui::End();
        return;
    }

    const CircuitSolution* solution = m_solved ? &m_solution : nullptr;
    const Node* a = m_circuit.findNode(component->nodeA);
    const Node* b = m_circuit.findNode(component->nodeB);
    double length = (a && b) ? (b->position - a->position).length() : 0.0;
    double va = potentialFor(solution, component->nodeA);
    double vb = potentialFor(solution, component->nodeB);
    const BranchResult* br = branchFor(solution, component->id);
    double dV = br ? br->voltageDrop : (va - vb);
    double current = br ? br->current : 0.0;
    double power = br ? br->power : 0.0;

    char name[32];
    const char* prefix = "X";
    if (component->type == ComponentType::Resistor) prefix = "R";
    else if (component->type == ComponentType::VoltageSource) prefix = "V";
    else if (component->type == ComponentType::Wire) prefix = "W";
    else if (component->type == ComponentType::Ground) prefix = "GND";
    else if (component->type == ComponentType::Capacitor) prefix = "C";
    else if (component->type == ComponentType::Inductor) prefix = "L";
    else if (component->type == ComponentType::Diode) prefix = "D";
    else if (component->type == ComponentType::Switch) prefix = "S";
    std::snprintf(name, sizeof(name), "%s%d", prefix, component->id);

    ImGui::Text("Name: %s", name);
    ImGui::TextDisabled("ComponentId: %d", component->id);
    ImGui::Separator();

    if (component->type == ComponentType::Resistor) {
        ImGui::InputDouble(tr("Resistance (Ohm)"), &m_elementEdit.pendingValue, 10.0, 100.0, "%.3f");
        ImGui::Text("Length: %.3f wu", length);
        const char* materials[] = {"Copper", "Nichrome", "Custom"};
        ImGui::Combo(tr("Material"), &m_elementEdit.pendingMaterial, materials, IM_ARRAYSIZE(materials));
    } else if (component->type == ComponentType::VoltageSource) {
        ImGui::InputDouble(tr("Voltage (V)"), &m_elementEdit.pendingValue, 0.1, 1.0, "%.3f");
        ImGui::TextDisabled("%s", tr("Internal resistance: ideal source in current model"));
    } else if (component->type == ComponentType::Wire) {
        // Wire parameters are global model settings; edit them live so the
        // canvas updates immediately, no Apply required.
        ImGui::Text("Length: %.3f wu", length);
        if (ImGui::InputDouble(tr("R per unit"), &m_wireResistancePerUnit, 0.01, 0.1, "%.4f")) {
            m_wireResistancePerUnit = std::max(0.0, m_wireResistancePerUnit);
            onCircuitChanged();
        }
        ImGui::Text("Total R: %.4f Ohm",
                    current_lab::physics::wireResistance(length, m_wireResistancePerUnit));
        if (ImGui::InputInt(tr("Distributed segments"), &m_distributedSegments)) {
            m_distributedSegments = std::clamp(m_distributedSegments, 1, 64);
            onCircuitChanged();
        }
    } else if (component->type == ComponentType::Capacitor) {
        double uF = m_elementEdit.pendingValue * 1e6;
        if (ImGui::InputDouble(tr("Capacitance (uF)"), &uF, 100.0, 1000.0, "%.1f"))
            m_elementEdit.pendingValue = std::max(1e-12, uF * 1e-6);
        ImGui::TextDisabled("tau = R*C; with 1 kOhm: %.3f s", m_elementEdit.pendingValue * 1000.0);
    } else if (component->type == ComponentType::Inductor) {
        ImGui::InputDouble(tr("Inductance (H)"), &m_elementEdit.pendingValue, 0.1, 1.0, "%.3f");
        ImGui::TextDisabled("tau = L/R; with 10 Ohm: %.3f s",
                            std::max(0.0, m_elementEdit.pendingValue) / 10.0);
    } else if (component->type == ComponentType::Diode) {
        ImGui::TextUnformatted(tr("Ideal piecewise-linear diode"));
        ImGui::TextDisabled("%s", tr("Conducts A -> B when forward biased; no forward drop."));
        bool conducting = std::abs(dV) < 1e-6 && current > 1e-12;
        ImGui::Text(tr("State: %s"), conducting ? tr("conducting") : tr("blocking"));
    } else if (component->type == ComponentType::Switch) {
        bool closed = component->value >= 0.5;
        if (ImGui::Checkbox(tr("Closed"), &closed)) {
            component->value = closed ? 1.0 : 0.0;
            onCircuitChanged();
        }
    } else if (component->type == ComponentType::Ground) {
        ImGui::TextUnformatted(tr("Reference node"));
        if (ImGui::Button(tr("Set as reference"))) {
            m_circuit.groundNodeId = component->nodeB;
            onCircuitChanged();
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(tr("Solved values"));
    ImGui::Text("Va: %.4f %s", va, tr("V"));
    ImGui::Text("Vb: %.4f %s", vb, tr("V"));
    ImGui::Text("dV: %.4f %s", dV, tr("V"));
    ImGui::Text("I: %.4f %s", milliamps(current), tr("mA"));
    ImGui::Text("P: %.4f %s", milliwatts(power), tr("mW"));
    if (component->type == ComponentType::Capacitor)
        ImGui::Text("E = 1/2 C V^2 = %.4f mJ",
                    current_lab::physics::capacitorEnergy(component->value, dV) * 1000.0);
    if (component->type == ComponentType::Inductor)
        ImGui::Text("E = 1/2 L I^2 = %.4f mJ",
                    current_lab::physics::inductorEnergy(component->value, current) * 1000.0);

    ImGui::Spacing();
    bool deleteRequested = false;
    if (ImGui::Button(tr("Delete"), ImVec2(86, 0)))
        deleteRequested = true;
    ImGui::SameLine();
    if (ImGui::Button(tr("Apply"), ImVec2(86, 0))) {
        if (component->type == ComponentType::Resistor) {
            component->value = std::max(1e-9, m_elementEdit.pendingValue);
        } else if (component->type == ComponentType::VoltageSource) {
            component->value = m_elementEdit.pendingValue;
        } else if (component->type == ComponentType::Capacitor ||
                   component->type == ComponentType::Inductor) {
            component->value = std::max(1e-12, m_elementEdit.pendingValue);
        }
        onCircuitChanged();
        m_elementEdit.close();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(86, 0))) {
        m_elementEdit.close();
    }

    if (deleteRequested) {
        int id = component->id;
        m_circuit.removeComponent(id);
        if (m_selComp == id)
            m_selComp = -1;
        m_dualView.clearSelection();
        m_elementEdit.close();
        onCircuitChanged();
    }

    ImGui::End();
}

void MainWindow::renderBottomAnalysis(const DistributedWireParameters& params) {
    const CircuitSolution* solution = m_solved ? &m_solution : nullptr;
    const Component* selectedComp = m_circuit.findComponent(m_selComp);
    const Node* a = selectedComp ? m_circuit.findNode(selectedComp->nodeA) : nullptr;
    const Node* b = selectedComp ? m_circuit.findNode(selectedComp->nodeB) : nullptr;
    double va = selectedComp ? potentialFor(solution, selectedComp->nodeA) : 0.0;
    double vb = selectedComp ? potentialFor(solution, selectedComp->nodeB) : 0.0;
    const BranchResult* br = selectedComp ? branchFor(solution, selectedComp->id) : nullptr;
    double dV = br ? br->voltageDrop : (selectedComp ? va - vb : 0.0);
    double current = br ? br->current : 0.0;
    double power = br ? br->power : 0.0;
    double length = (a && b) ? (b->position - a->position).length() : 0.0;
    double e = length > 1e-9 ? std::abs(dV) / length : 0.0;

    float height = m_debugMode && m_showDebugLog ? 232.0f : m_bottomHeight;
    ImGui::BeginChild("BottomAnalysis", ImVec2(0, height), ImGuiChildFlags_Border);
    if (ImGui::BeginTable("AnalysisStrip", 5, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(tr("Potential V(x)"));
        if (selectedComp && selectedComp->type != ComponentType::Ground) {
            float values[48];
            for (int i = 0; i < 48; ++i) {
                double t = static_cast<double>(i) / 47.0;
                values[i] = static_cast<float>(va + (vb - va) * t);
            }
            double lo = std::min(va, vb) - 0.1;
            double hi = std::max(va, vb) + 0.1;
            ImGui::PlotLines("##Vx", values, 48, 0, nullptr, static_cast<float>(lo), static_cast<float>(hi), ImVec2(-1, 42));
            ImGui::Text("%.3f -> %.3f V", va, vb);
        } else {
            ImGui::TextDisabled("%s", tr("Select a path element"));
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(tr("Current I"));
        ImGui::Text("%.4f %s", milliamps(current), tr("mA"));
        ImGui::TextDisabled("%s", m_electronFlow ? tr("electron drift shown") : tr("conventional shown"));

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(tr("Voltage Drop dV"));
        ImGui::Text("%.4f %s", dV, tr("V"));
        ImGui::TextDisabled("E ~= %.5f V/wu", e);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(tr("Power P"));
        ImGui::Text("%.4f %s", milliwatts(power), tr("mW"));
        ImGui::TextDisabled("%s", selectedComp && current_lab::physics::isSupplyingPower(power)
                                       ? tr("supplied") : tr("dissipated/local"));

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(tr("Model Status"));
        if (m_simMode == SimulationMode::Transient) {
            ImGui::TextWrapped("Transient: %s, dt = %.3g ms, t = %.3f s",
                               m_integrationMethod == IntegrationMethod::BackwardEuler
                                   ? "backward Euler" : "trapezoidal",
                               m_transientDt * 1000.0, m_transientState.time);
        } else {
            ImGui::TextWrapped("%s", tr("DC steady-state"));
        }
        ImGui::TextWrapped("%s", tr("Lumped circuit + distributed 1D wire"));
        ImGui::TextDisabled("Surface charge: %s", m_showSurfaceCharge ? "heuristic" : "hidden");
        ImGui::TextDisabled("Magnetic: %s", m_showMagnetic ? "qualitative" : "hidden");
        ImGui::EndTable();
    }

    if (m_debugMode && m_showDebugLog) {
        ImGui::SeparatorText("Debug Log");
        ImGui::BeginChild("DebugLogScroll", ImVec2(0, 0), 0);
        m_inspector.log().render();
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void MainWindow::renderToolbar() {
    renderToolRail();
}

void MainWindow::renderLog() {
    ImGui::BeginChild("LogPanel", ImVec2(0, m_logHeight), ImGuiChildFlags_Border);
    m_inspector.log().render();
    ImGui::EndChild();
}
