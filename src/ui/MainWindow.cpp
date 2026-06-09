#include "ui/MainWindow.h"
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

MainWindow::MainWindow() {
    wireCallbacks();
    m_inspector.onChange = [this]() { onCircuitChanged(); };
    applyVisualizationPreset(m_visualPreset);
    setupTestCircuit();
    runSolver();
}

void MainWindow::wireCallbacks() {
    m_canvas.callbacks.placeNode = [this](Vec2 pos) {
        m_circuit.addNode(pos);
        onCircuitChanged();
    };
    m_canvas.callbacks.createComponent = [this](int from, int to, ComponentType type, double val) {
        int id = m_circuit.addComponent(type, from, to, val);
        if (type == ComponentType::Ground) m_circuit.groundNodeId = to;
        m_selNode = -1;
        m_selComp = id;
        onCircuitChanged();
    };
    m_canvas.callbacks.selectNode = [this](int id) {
        m_selNode = id; m_selComp = -1;
    };
    m_canvas.callbacks.selectComponent = [this](int id) {
        m_selComp = id; m_selNode = -1;
    };
    m_canvas.callbacks.deselect = [this]() {
        m_selNode = -1; m_selComp = -1;
    };
    m_canvas.callbacks.moveNode = [this](int id, Vec2 pos) {
        Node* n = m_circuit.findNode(id);
        if (n) { n->position = pos; onCircuitChanged(); }
    };
    m_canvas.callbacks.deleteSelected = [this]() {
        if (m_selComp >= 0) {
            m_circuit.removeComponent(m_selComp);
            m_selComp = -1;
            onCircuitChanged();
        } else if (m_selNode >= 0) {
            m_circuit.removeNode(m_selNode);
            m_selNode = -1;
            onCircuitChanged();
        }
    };
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
    m_distributedSolution = m_solver.solve(m_distributedCircuit);
    mapDistributedSolution();
    m_solved = true;
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

    DistributedWireParameters params;
    params.segmentsPerWire = m_distributedSegments;
    params.resistancePerUnit = m_wireResistancePerUnit;

    renderTopBar();

    float bottomHeight = m_debugMode && m_showDebugLog ? 232.0f : m_bottomHeight;
    float availY = std::max(220.0f, ImGui::GetContentRegionAvail().y - bottomHeight - 8.0f);

    m_canvas.setMode(m_mode);
    m_canvas.setSelected(m_selNode, m_selComp);
    m_canvas.setShowCurrent(m_showCurrent);
    m_canvas.setElectronFlow(m_electronFlow);
    m_canvas.setShowPotential(m_showPotential);
    m_canvas.setShowDrift(m_showDrift);
    m_canvas.setShowEField(m_showEField);
    m_canvas.setShowHeat(m_showHeat);
    m_canvas.setShowPower(m_showPower);
    m_canvas.setShowMagnetic(m_showMagnetic);
    m_canvas.setShowSurfaceCharge(m_showSurfaceCharge);
    m_canvas.setDebugView(m_debugMode);
    m_canvas.setShowCanvasReadouts(m_showCanvasReadouts);
    m_canvas.setWireThickness(m_wireThickness);
    m_canvas.setReadOnly(false);

    ImGui::BeginChild("ToolRail", ImVec2(m_leftWidth, availY), ImGuiChildFlags_Border);
    renderToolRail();
    ImGui::EndChild();

    ImGui::SameLine();

    float remainingX = ImGui::GetContentRegionAvail().x;
    float canvasWidth = std::max(260.0f, remainingX - m_rightWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginChild("CanvasContainer", ImVec2(canvasWidth, availY), 0,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    m_canvas.render(m_circuit, m_solved ? &m_solution : nullptr);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("RightInspector", ImVec2(m_rightWidth, availY), ImGuiChildFlags_Border);
    renderRightInspector(params);
    ImGui::EndChild();

    renderBottomAnalysis(params);

    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

void MainWindow::renderTopBar() {
    static const char* presetLabels[] = {
        "Circuit",
        "Potential",
        "Electric Field",
        "Current / Drift",
        "Power / Heat",
        "Charges",
        "Debug",
    };

    ImGui::BeginChild("TopBar", ImVec2(0, 44), ImGuiChildFlags_Border);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Current Lab");
    ImGui::SameLine();
    ImGui::TextDisabled("Workspace");

    ImGui::SameLine(210.0f);
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::Combo("##VisualizationPreset", &m_visualPreset, presetLabels, IM_ARRAYSIZE(presetLabels)))
        applyVisualizationPreset(m_visualPreset);

    ImGui::SameLine();
    if (ImGui::Button("Run Solver")) {
        runSolver();
        m_inspector.log().addMessage("Solver run manually.");
    }

    ImGui::SameLine();
    bool paused = m_canvas.animationPaused();
    if (ImGui::Checkbox("Pause", &paused))
        m_canvas.setAnimationPaused(paused);

    ImGui::SameLine();
    bool debug = m_debugMode;
    if (ImGui::Checkbox("Debug", &debug)) {
        applyVisualizationPreset(debug
            ? static_cast<int>(current_lab::visualization::VisualizationPreset::Debug)
            : static_cast<int>(current_lab::visualization::VisualizationPreset::Circuit));
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Model: DC steady-state / lumped + distributed 1D wire");
    ImGui::EndChild();
}

void MainWindow::renderToolRail() {
    ImGui::TextDisabled("Tools");
    ImGui::Separator();

    EditorMode modes[] = {
        EditorMode::Select,
        EditorMode::PlaceNode,
        EditorMode::PlaceWire,
        EditorMode::PlaceResistor,
        EditorMode::PlaceVoltageSource,
        EditorMode::PlaceGround,
    };

    for (auto m : modes) {
        bool active = (m_mode == m);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(79, 101, 88, 255));
        if (ImGui::Button(modeLabel(m), ImVec2(-1, 30)))
            m_mode = m;
        if (active)
            ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Read");
    if (ImGui::Button("Probe", ImVec2(-1, 30)))
        m_mode = EditorMode::Select;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Readout follows the selected node or element in this pass.");
    if (ImGui::Button("Pan", ImVec2(-1, 30)))
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

    ImGui::SeparatorText("Visualization");
    ImGui::Text("Mode: %s", m_debugMode ? "Debug" : "Learner");
    ImGui::Text("Preset: %s", info.label);
    ImGui::TextWrapped("%s", info.modelNote);
    renderLayerState("Potential", m_showPotential, VisualizationLayer::Potential);
    renderLayerState("E-field", m_showEField, VisualizationLayer::ElectricField);
    renderLayerState("Current", m_showCurrent, VisualizationLayer::Current);
    renderLayerState("Drift", m_showDrift, VisualizationLayer::Drift);
    renderLayerState("Heat", m_showHeat, VisualizationLayer::Heat);
    renderLayerState("Surface charge", m_showSurfaceCharge, VisualizationLayer::SurfaceCharge);
    renderLayerState("Magnetic", m_showMagnetic, VisualizationLayer::MagneticField);

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
    ImGui::SeparatorText("Probe Readout");
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
        ImGui::TextDisabled("Probe position");
        ImGui::Text("Selected %s midpoint", componentTypeLabel(selectedComp->type));
        ImGui::Text("V: %.4f -> %.4f V", va, vb);
        ImGui::Text("E: %.5f V/wu", e);
        ImGui::Text("I: %.4f mA", milliamps(current));
        ImGui::Text("P local: %.4f mW", milliwatts(current_lab::physics::dissipatedPowerOnly(selectedComp->type, power)));
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
        ImGui::TextDisabled("Select a node or component for numerical readout.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Selected Element");
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

        ImGui::Text("Type: %s", componentTypeLabel(selectedComp->type));
        ImGui::Text("Name: %s %d", componentTypeLabel(selectedComp->type), selectedComp->id);
        ImGui::Text("Node A: %d", selectedComp->nodeA);
        ImGui::Text("Node B: %d", selectedComp->nodeB);
        ImGui::Text("Va: %.4f V", va);
        ImGui::Text("Vb: %.4f V", vb);
        ImGui::Text("dV: %.4f V", dV);
        ImGui::Text("I: %.4f mA", milliamps(current));
        if (selectedComp->type == ComponentType::Resistor)
            ImGui::Text("R: %.4f Ohm", selectedComp->value);
        if (selectedComp->type == ComponentType::VoltageSource)
            ImGui::Text("Source: %.4f V", selectedComp->value);
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
        ImGui::TextDisabled("Nothing selected.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Simulation Controls");
    if (ImGui::Button("Run", ImVec2(72, 0))) {
        runSolver();
        m_inspector.log().addMessage("Solver run manually.");
    }
    ImGui::SameLine();
    bool paused = m_canvas.animationPaused();
    if (ImGui::Checkbox("Pause", &paused))
        m_canvas.setAnimationPaused(paused);
    if (ImGui::Button("Reset Time"))
        m_canvas.resetAnimationTime();
    float speed = m_canvas.animationSpeed();
    if (ImGui::SliderFloat("Animation speed", &speed, 0.0f, 4.0f, "%.2fx"))
        m_canvas.setAnimationSpeed(speed);
    ImGui::Text("Visual speed multiplier: %.2fx", m_canvas.animationSpeed());
    ImGui::SliderFloat("Wire width", &m_wireThickness, 2.0f, 50.0f, "%.1f wu");
    if (ImGui::SliderInt("Wire segments", &m_distributedSegments, 1, 32))
        onCircuitChanged();
    if (ImGui::InputDouble("R / unit", &m_wireResistancePerUnit, 0.05, 0.5, "%.3f")) {
        if (m_wireResistancePerUnit < 0.0)
            m_wireResistancePerUnit = 0.0;
        onCircuitChanged();
    }
    bool debug = m_debugMode;
    if (ImGui::Checkbox("Debug mode", &debug)) {
        applyVisualizationPreset(debug
            ? static_cast<int>(VisualizationPreset::Debug)
            : static_cast<int>(VisualizationPreset::Circuit));
    }
    if (ImGui::Button("Clear", ImVec2(72, 0))) {
        m_circuit = Circuit{};
        m_distributedCircuit = Circuit{};
        m_solution = CircuitSolution{};
        m_distributedSolution = CircuitSolution{};
        m_selNode = -1;
        m_selComp = -1;
        m_solved = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Demo")) {
        setupTestCircuit();
        m_selNode = -1;
        m_selComp = -1;
        runSolver();
    }

    if (m_debugMode && ImGui::CollapsingHeader("Verbose Inspector")) {
        m_inspector.render(m_circuit, solution, m_selNode, m_selComp, params,
                           m_wireThickness, m_canvas.animationSpeed(), m_electronFlow);
    }
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
        ImGui::TextUnformatted("Potential V(x)");
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
            ImGui::TextDisabled("Select a path element");
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Current I");
        ImGui::Text("%.4f mA", milliamps(current));
        ImGui::TextDisabled(m_electronFlow ? "electron drift shown" : "conventional shown");

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Voltage Drop dV");
        ImGui::Text("%.4f V", dV);
        ImGui::TextDisabled("E ~= %.5f V/wu", e);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Power P");
        ImGui::Text("%.4f mW", milliwatts(power));
        ImGui::TextDisabled(selectedComp && current_lab::physics::isSupplyingPower(power) ? "supplied" : "dissipated/local");

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Model Status");
        ImGui::TextWrapped("DC steady-state");
        ImGui::TextWrapped("Lumped circuit + distributed 1D wire");
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
