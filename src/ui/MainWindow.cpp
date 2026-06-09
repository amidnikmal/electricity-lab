#include "ui/MainWindow.h"
#include "ui/Format.h"
#include <cstdio>

MainWindow::MainWindow() {
    wireCallbacks();
    m_inspector.onChange = [this]() { onCircuitChanged(); };
    setupTestCircuit();
    runSolver();
}

void MainWindow::wireCallbacks() {
    m_canvas.callbacks.placeNode = [this](Vec2 pos) {
        m_circuit.addNode(pos);
        onCircuitChanged();
    };
    m_canvas.callbacks.createComponent = [this](int from, int to, ComponentType type, double val) {
        m_circuit.addComponent(type, from, to, val);
        if (type == ComponentType::Ground) m_circuit.groundNodeId = to;
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
    m_distributedCircuit = m_circuit.toDistributed(8);
    m_distributedSolution = m_solver.solve(m_distributedCircuit);
    mapDistributedSolution();
    m_solved = true;
}

void MainWindow::mapDistributedSolution() {
    m_solution.nodePotentials.clear();
    int origNodeCount = (int)m_circuit.nodes.size();

    for (const auto& np : m_distributedSolution.nodePotentials) {
        if (np.nodeId >= 0 && np.nodeId < origNodeCount)
            m_solution.nodePotentials.push_back(np);
    }

    for (int i = (int)m_circuit.nodes.size(); i < (int)m_distributedCircuit.nodes.size(); ++i) {
        SolutionPoint sp;
        sp.nodeId = -1;
        sp.potential = 0.0;
        for (const auto& np : m_distributedSolution.nodePotentials) {
            if (np.nodeId == i) { sp.potential = np.potential; break; }
        }
    }

    m_solution.branches.clear();
    int origCount = (int)m_circuit.components.size();
    for (int oi = 0; oi < origCount; ++oi) {
        BranchResult br;
        br.componentId = oi;

        const auto& oc = m_circuit.components[oi];
        bool isWire = (oc.type == ComponentType::Wire);

        double totalCurrent = 0.0;
        double totalVdrop = 0.0;
        double totalPower = 0.0;
        int segCount = 0;

        for (int di = 0; di < (int)m_distributedSolution.branches.size(); ++di) {
            int srcIdx = di < (int)m_distributedCircuit.distributedSource.size()
                             ? m_distributedCircuit.distributedSource[di] : -1;
            if (srcIdx != oi) continue;

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

static const char* modeLabel(EditorMode m) {
    switch (m) {
        case EditorMode::Select:           return "Select";
        case EditorMode::PlaceNode:        return "Node";
        case EditorMode::PlaceWire:        return "Wire";
        case EditorMode::PlaceResistor:    return "Resistor";
        case EditorMode::PlaceVoltageSource: return "V src";
        case EditorMode::PlaceGround:      return "Ground";
    }
    return "?";
}

void MainWindow::render() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::Begin("MainWindow", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings);

    float availY = ImGui::GetContentRegionAvail().y - m_logHeight - 4;

    ImGui::BeginChild("LeftPanel", ImVec2(m_leftWidth, availY), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
    renderToolbar();
    ImGui::EndChild();

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
    m_canvas.setWireThickness(m_wireThickness);
    m_canvas.setReadOnly(false);

    ImGui::SameLine();

    ImGui::BeginChild("RightPanel", ImVec2(m_rightWidth, availY), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
    m_inspector.render(m_circuit, m_solved ? &m_solution : nullptr, m_selNode, m_selComp);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("CanvasContainer", ImVec2(0, availY), 0,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    m_canvas.render(m_circuit, m_solved ? &m_solution : nullptr);
    ImGui::EndChild();

    renderLog();
    ImGui::End();
}

void MainWindow::renderToolbar() {
    ImGui::TextUnformatted("Mode");
    ImGui::Separator();

    EditorMode modes[] = {
        EditorMode::Select, EditorMode::PlaceNode, EditorMode::PlaceWire,
        EditorMode::PlaceResistor, EditorMode::PlaceVoltageSource, EditorMode::PlaceGround
    };
    for (auto m : modes) {
        bool active = (m_mode == m);
        if (ImGui::Selectable(modeLabel(m), active)) {
            m_mode = m;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Run Solver", ImVec2(-1, 0))) runSolver();
    if (ImGui::Button("Clear Circuit", ImVec2(-1, 0))) {
        m_circuit = Circuit{};
        m_selNode = -1; m_selComp = -1;
        m_solved = false;
    }
    if (ImGui::Button("Reset Demo", ImVec2(-1, 0))) {
        setupTestCircuit();
        m_selNode = -1; m_selComp = -1;
        runSolver();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Checkbox("Show Current", &m_showCurrent);
    ImGui::Checkbox("Electron Flow", &m_electronFlow);
    ImGui::Checkbox("Show Potential", &m_showPotential);
    ImGui::Checkbox("Show Drift", &m_showDrift);
    ImGui::Checkbox("Show E-field", &m_showEField);
    ImGui::Checkbox("Show Heat", &m_showHeat);
    ImGui::Checkbox("Show Power", &m_showPower);
    ImGui::Checkbox("Show Magnetic", &m_showMagnetic);
    ImGui::Checkbox("Surface Charge", &m_showSurfaceCharge);
    ImGui::SliderFloat("Wire Width", &m_wireThickness, 2.0f, 50.0f, "%.1f wu");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Controls:");
    ImGui::BulletText("L-click: place/select");
    ImGui::BulletText("M-drag: pan");
    ImGui::BulletText("Scroll: zoom");
    ImGui::BulletText("Del: remove");
}

void MainWindow::renderLog() {
    ImGui::BeginChild("LogPanel", ImVec2(0, m_logHeight), ImGuiChildFlags_Border);
    m_inspector.log().render();
    ImGui::EndChild();
}
