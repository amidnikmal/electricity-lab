#include "ui/MainWindow.h"
#include "ui/I18n.h"
#include "ui/UiHelpers.h"
#include "circuit/DemoCircuits.h"
#include "projection/MechanicsMapping.h"
#include "projection/MechanicsCoupling.h"
#include "visualization/VisualizationStatus.h"
#include "visualization/VisualizationPresets.h"
#include "ui/Format.h"
#include "physics/ChainGeometry.h"
#include "physics/ChainSim.h"
#include "physics/ChannelSpecs.h"
#include "physics/DriftModel.h"
#include "physics/PowerModel.h"
#include "physics/WirePhysics.h"
#include <algorithm>
#include <chrono>
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
        // Новая цепь = новое id-пространство: старый заряд не должен
        // прилипнуть к чужим компонентам.
        m_liveSim.discharge(); m_thermal.reset(); resetMechanicsPhases();
        onCircuitChanged();
    };
    applyVisualizationPreset(m_visualPreset);
    setupTestCircuit();
    circuitEvent();
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
        canvas.callbacks.crankBegin = [this](int componentId) {
            // The crank is a TEMPORARY drive: remember the source setting.
            if (Component* comp = m_circuit.findComponent(componentId)) {
                m_crankSavedComponent = componentId;
                m_crankSavedValue = comp->value;
            }
        };
        canvas.callbacks.crankEnd = [this](int componentId) {
            // Released: the source returns to its configured EMF, so the
            // circuit never gets stuck at ~0 V after a slow release.
            if (m_crankSavedComponent == componentId) {
                if (Component* comp = m_circuit.findComponent(componentId))
                    comp->value = m_crankSavedValue;
                m_crankSavedComponent = -1;
                circuitEvent();
            }
        };
        canvas.callbacks.driveSource = [this](int componentId, double omega) {
            Component* comp = m_circuit.findComponent(componentId);
            if (!comp || comp->type != ComponentType::VoltageSource) return;
            double target = current_lab::mechanics::emfFromCrankSpeed(omega);
            // Light smoothing so the EMF follows the hand without jitter.
            double next = comp->value * 0.7 + target * 0.3;
            if (std::abs(next - comp->value) > 0.02) {
                comp->value = next;
                // Живая ЭДС каждый кадр: будим без тевенин-проб (tau не
                // менялась — изменилось только значение источника).
                rebuildDistributed();
                m_liveSim.wakeKeepSpeed();
                refreshSolution();
            }
        };
        canvas.callbacks.toggleSwitch = [this](int componentId) {
            // Щелчок хот-зоны: эксперимент, не редактирование — выделение и
            // редактор не трогаем, заряд конденсаторов сохраняется.
            Component* comp = m_circuit.findComponent(componentId);
            if (!comp || comp->type != ComponentType::Switch) return;
            bool nowClosed = !(comp->value >= 0.5);
            comp->value = nowClosed ? 1.0 : 0.0;
            circuitEvent();
            m_inspector.log().addMessage(nowClosed ? "Switch closed." : "Switch opened.");
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
    circuitEvent();
    m_inspector.log().addMessage("Circuit changed, re-solving...");
}

void MainWindow::setupTestCircuit() {
    m_circuit = Circuit{};
    int gnd = m_circuit.addNode(Vec2(200, 300), "GND");
    int n1  = m_circuit.addNode(Vec2(200, 150), "N1");
    int n2  = m_circuit.addNode(Vec2(450, 150), "N2");
    m_circuit.groundNodeId = gnd;
    int corner = m_circuit.addNode(Vec2(450, 300));
    m_circuit.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    m_circuit.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);
    m_circuit.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
    m_circuit.addComponent(ComponentType::Wire, n2, corner, 0.0);
    m_circuit.addComponent(ComponentType::Wire, corner, gnd, 0.0);
}

void MainWindow::rebuildDistributed() {
    DistributedWireParameters params;
    params.segmentsPerWire = m_distributedSegments;
    params.resistancePerUnit = m_wireResistancePerUnit;
    m_distributedCircuit = m_circuit.toDistributed(params);
}

void MainWindow::refreshSolution() {
    m_distributedSolution = m_liveSim.currentSolution(m_distributedCircuit, m_solver);
    mapDistributedSolution();
    m_solved = true;
}

void MainWindow::runSolver() {
    rebuildDistributed();
    refreshSolution();
}

// Единая воронка событий: любая правка цепи (значение, топология, щелчок
// выключателя, ручка-динамо) будит LiveSim и пересчитывает авто-замедление.
// Заряд C/L при этом сохраняется — это физика, а не сброс.
void MainWindow::circuitEvent() {
    rebuildDistributed();
    m_liveSim.onCircuitEvent(m_distributedCircuit, m_solver);
    refreshSolution();
}

void MainWindow::updateParticleSim(float realDt) {
    const CircuitSolution* solution = m_solved ? &m_solution : nullptr;
    double particleRadius = current_lab::physics::particleWorldRadius(m_wireThickness);
    double dt = m_animationPaused ? 0.0 : static_cast<double>(realDt) * m_animationSpeed;

    // Each Box2D world is expensive (hundreds of bodies at 120 Hz substeps);
    // step only the worlds some visible pane actually renders. A world keeps
    // its state while hidden and resumes seamlessly when its pane returns.
    bool needElectrons = false, needWater = false, needChains = false;
    for (int paneId : m_paneTree.paneIds()) {
        auto kind = static_cast<current_lab::projection::ProjectionKind>(
            m_paneTree.projectionOf(paneId));
        needElectrons |= kind == current_lab::projection::ProjectionKind::Physics;
        needWater |= kind == current_lab::projection::ProjectionKind::Hydraulic;
        needChains |= kind == current_lab::projection::ProjectionKind::Mechanical;
    }

    auto runWorld = [&](current_lab::physics::ParticleSim& sim, bool waterWorld,
                        std::vector<current_lab::physics::SimParticle>& outParticles) {
        auto specs = current_lab::physics::makeChannelSpecs(m_circuit, solution,
                                                            m_wireThickness, waterWorld);
        if (specs.empty()) {
            outParticles.clear();
            return;
        }
        if (!sim.configured())
            sim.configure(specs, particleRadius);
        else
            sim.setTargets(specs); // re-configures itself on layout change
        if (dt > 0.0)
            sim.step(dt);
        outParticles = sim.particles();
    };

    if (needElectrons)
        runWorld(m_electronSim, /*waterWorld=*/false, m_electronParticles);
    if (needWater) {
        runWorld(m_waterSim, /*waterWorld=*/true, m_waterParticles);
        m_waterPaddles = m_waterSim.paddles();
    }

    // Mechanics chain: rigid-jointed loops, one per component.
    std::vector<current_lab::physics::ChainSpec> chainSpecs;
    if (!needChains) {
        current_lab::projection::advanceFlowIntegrals(m_flowIntegrals, m_circuit,
                                                      solution, dt);
        return;
    }
    // Rigid-axle coupling: every chain in one connected mechanism shares a
    // single rotation sign, so gears on a shared node spin together instead of
    // fighting (see MechanicsCoupling.h). Computed once per frame from the SAME
    // model + solution the chains are built from.
    m_axleCoupling = current_lab::mechanics::computeAxleCoupling(m_circuit, solution);
    for (const auto& comp : m_circuit.components) {
        if (comp.type == ComponentType::Ground)
            continue;
        if (comp.type == ComponentType::Switch && comp.value < 0.5)
            continue;
        const Node* a = m_circuit.findNode(comp.nodeA);
        const Node* b = m_circuit.findNode(comp.nodeB);
        if (!a || !b) continue;
        double current = 0.0;
        if (solution) {
            for (const auto& br : solution->branches)
                if (br.componentId == comp.id) { current = br.current; break; }
        }
        // ONE visual scale for the WHOLE mechanics view (every component incl.
        // the capacitor). This is the single "speed knob": big enough the chain
        // reads as fast as the electron drift, small enough that a capacitor's
        // shaft (driven by ∫chain travel) winds its spring within the crank's
        // ±θmax over a normal charge — that is what lets gear, arm and spring
        // stay one rigid body AND stay synced with the loop. Direction comes from
        // the rigid-axle coupling, not this component's nodeA->nodeB order.
        double mappedSpeed = current_lab::mechanics::chainSpeedFromCurrent(current) *
                             current_lab::mechanics::kVisualChainSpeed *
                             current_lab::mechanics::kMechChainBoost;
        double targetSpeed = std::clamp(
            m_axleCoupling.signFor(comp.id) * std::abs(mappedSpeed),
            -120.0, 120.0);
        // Honest chain travel for every component, capacitor included — the
        // wheels/leads read this so they all move together.
        m_chainTravel[comp.id] += targetSpeed * dt;

        // The capacitor is NOT a chain oval through the device (the spring sits
        // between two independent shafts); it is driven by chainTravel in
        // emitSpring. Every other component gets a ChainSim loop.
        if (comp.type == ComponentType::Capacitor)
            continue;

        current_lab::physics::ChainSpec spec;
        spec.componentId = comp.id;
        spec.a = a->position;
        spec.b = b->position;
        spec.halfWidth = current_lab::physics::chain_geometry::chainHalfWidth(m_wireThickness);
        spec.targetSpeed = targetSpeed;
        spec.brake = comp.type == ComponentType::Resistor;
        spec.driveSprocket = comp.type == ComponentType::VoltageSource;
        chainSpecs.push_back(spec);
    }
    if (chainSpecs.empty()) {
        m_chainLinks.clear();
    } else {
        if (!m_chainSim.configured())
            m_chainSim.configure(chainSpecs,
                                 current_lab::physics::chain_geometry::linkRadius(m_wireThickness));
        else
            m_chainSim.setTargets(chainSpecs);
        if (dt > 0.0)
            m_chainSim.step(dt);
        m_chainLinks = m_chainSim.links();
    }

    // Continuous wheel phases (theta = k * ∫I dt).
    current_lab::projection::advanceFlowIntegrals(m_flowIntegrals, m_circuit, solution, dt);
}

void MainWindow::advanceLiveSim(float realDt) {
    // Единая пауза останавливает и время цепи, и визуальные миры.
    if (m_animationPaused) return;
    if (m_liveSim.advance(m_distributedCircuit, m_solver, realDt, m_distributedSolution)) {
        mapDistributedSolution();
        m_solved = true;
        current_lab::physics::stepThermal(m_thermal, m_distributedCircuit, m_distributedSolution, m_liveSim.dt());
        current_lab::physics::ThermalState aggregated;
        for (const auto& c : m_circuit.components)
            if (c.type != ComponentType::Ground)
                aggregated.temperature[c.id] = elementTemperatureK(c.id);
        m_recorder.sample(m_solution, aggregated, m_liveSim.time());
    }
}

void MainWindow::stepLiveSimOnce() {
    m_liveSim.stepOnce(m_distributedCircuit, m_solver, m_distributedSolution);
    mapDistributedSolution();
    m_solved = true;
    current_lab::physics::stepThermal(m_thermal, m_distributedCircuit, m_distributedSolution, m_liveSim.dt());
    current_lab::physics::ThermalState aggregated;
    for (const auto& c : m_circuit.components)
        if (c.type != ComponentType::Ground)
            aggregated.temperature[c.id] = elementTemperatureK(c.id);
    m_recorder.sample(m_solution, aggregated, m_liveSim.time());
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

    // distributedSource is aligned with m_distributedCircuit.components by
    // INDEX, but solver branches are NOT positional (Ground is skipped there)
    // — match branches by componentId. The old positional lookup shifted every
    // branch after Ground by one: the source got the RESISTOR's current and
    // the visual loop currents converged into the ground corner (user report
    // 2026-06-11 «ток течёт в одну точку — левый нижний угол»).
    auto originalIdFor = [&](int distributedComponentId) {
        int idx = m_distributedCircuit.componentIndex(distributedComponentId);
        if (idx < 0 || idx >= (int)m_distributedCircuit.distributedSource.size())
            return -1;
        return m_distributedCircuit.distributedSource[idx];
    };

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

        for (const auto& db : m_distributedSolution.branches) {
            if (originalIdFor(db.componentId) != oc.id) continue;

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

double MainWindow::elementTemperatureK(int originalComponentId) const {
    double hottest = current_lab::physics::kAmbientTemperature;
    for (int i = 0; i < (int)m_distributedCircuit.distributedSource.size(); ++i) {
        if (m_distributedCircuit.distributedSource[i] == originalComponentId) {
            double t = current_lab::physics::temperatureFor(m_thermal, m_distributedCircuit.components[i].id);
            if (t > hottest) hottest = t;
        }
    }
    return hottest;
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

    advanceLiveSim(ImGui::GetIO().DeltaTime);
    auto perfBlend = [](double& slot, std::chrono::steady_clock::time_point t0) {
        double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
        slot += (ms - slot) * 0.05; // EMA: читаемые, не дёргающиеся цифры
    };
    auto simT0 = std::chrono::steady_clock::now();
    updateParticleSim(ImGui::GetIO().DeltaTime);
    perfBlend(m_perfSimMs, simT0);

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
    auto panesT0 = std::chrono::steady_clock::now();
    renderDualCanvasArea(layout.canvasWidth, availY);
    perfBlend(m_perfPanesMs, panesT0);

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
    canvas.setSimParticles(nullptr);
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
    canvas.setSimParticles(&m_electronParticles);
    canvas.setPaddleStates(nullptr);
    canvas.setChainLinks(nullptr);
    canvas.setFlowIntegrals(&m_flowIntegrals);
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

void MainWindow::configureCanvasForMechanicsView(CircuitCanvas& canvas) {
    configureCanvasForPhysicsView(canvas);
    canvas.setSimParticles(nullptr); // mechanics uses the chain, not electrons
    canvas.setChainLinks(&m_chainLinks);
    canvas.setChainTravel(&m_chainTravel);
    canvas.setAxleCoupling(&m_axleCoupling);
    canvas.setProjection(current_lab::projection::ProjectionKind::Mechanical);
}

void MainWindow::configureCanvasForProjection(CircuitCanvas& canvas, int projection) {
    auto kind = static_cast<current_lab::projection::ProjectionKind>(projection);
    if (kind == current_lab::projection::ProjectionKind::Schematic) {
        configureCanvasForCircuitView(canvas);
    } else if (kind == current_lab::projection::ProjectionKind::Mechanical) {
        configureCanvasForMechanicsView(canvas);
    } else if (kind == current_lab::projection::ProjectionKind::Hydraulic) {
        configureCanvasForPhysicsView(canvas);
        canvas.setSimParticles(&m_waterParticles);
        canvas.setPaddleStates(&m_waterPaddles);
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

    const char* projections[] = {tr("Circuit"), tr("Physics"), tr("Mechanics"), tr("Water")};

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
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 3.0f));
        ImGui::SameLine();
        if (ImGui::Button("| |")) { splitRequest = leaf.paneId; splitSideBySide = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Split left/right"));
        ImGui::SameLine();
        if (ImGui::Button("=")) { splitRequest = leaf.paneId; splitSideBySide = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Split top/bottom"));
        if (m_paneTree.paneCount() > 1) {
            ImGui::SameLine();
            if (ImGui::Button("x"))
                closeRequest = leaf.paneId;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Close view"));
        }
        ImGui::PopStyleVar();

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

    // Единый живой режим: цепь всегда идёт во времени, «стационар» — предел
    // процесса, а не отдельный режим. Пауза останавливает всё (цепь + миры).
    ImGui::SameLine();
    if (ImGui::Button(m_animationPaused ? tr("Resume##sim") : tr("Pause##sim")))
        m_animationPaused = !m_animationPaused;
    ImGui::SameLine();
    if (ImGui::Button(tr("Step")))
        stepLiveSimOnce();
    ImGui::SameLine();
    if (ImGui::Button(tr("Discharge"))) {
        m_liveSim.discharge(); m_thermal.reset(); resetMechanicsPhases();
        refreshSolution();
        m_inspector.log().addMessage("Discharged: Vc = 0, Il = 0, t = 0.");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Vc = 0, Il = 0, t = 0"));
    ImGui::SameLine();
    ImGui::Text(tr("t = %.3f s"), m_liveSim.time());
    ImGui::SameLine();
    if (m_liveSim.settled()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "%s", tr("steady"));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tr("Process settled: the solver sleeps until the next event."));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "%s x%.4g",
                           tr("settling"), m_liveSim.simSpeed());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tr("Transient in slow motion: sim seconds per real second."));
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
    ImGui::SetNextItemWidth(96.0f);
    if (ImGui::BeginCombo("##Demos", tr("Demos"))) {
        using current_lab::demos::DemoCircuit;
        for (int d = 0; d < static_cast<int>(DemoCircuit::Count); ++d) {
            auto demo = static_cast<DemoCircuit>(d);
            if (ImGui::Selectable(tr(current_lab::demos::demoName(demo)))) {
                m_circuit = current_lab::demos::buildDemo(demo);
                m_selNode = -1;
                m_selComp = -1;
                m_dualView.clearSelection();
                m_elementEdit.close();
                // Живой режим: демка просто загружается разряженной и сама
                // проигрывает свой процесс (в авто-замедлении); никакого
                // переключения режимов больше нет.
                m_liveSim.discharge(); m_thermal.reset(); resetMechanicsPhases();
                m_fitDualViewsRequested = true;
                onCircuitChanged();
            }
        }
        ImGui::EndCombo();
    }

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
    ImGui::SeparatorText(tr("Thermometer"));
    if (selectedComp && selectedComp->type != ComponentType::Ground) {
        double tc = current_lab::physics::celsius(elementTemperatureK(selectedComp->id));
        ImGui::Text("%s %d: %.1f %s", tr(componentTypeLabel(selectedComp->type)), selectedComp->id, tc, tr("degC"));
    } else {
        ImGui::TextDisabled("%s", tr("Select an element for temperature."));
    }

    ImGui::Spacing();
    ImGui::SeparatorText(tr("Oscilloscope"));
    if (selectedComp && selectedComp->type != ComponentType::Ground) {
        char lbl[64];
        if (ImGui::SmallButton(tr("Pin I"))) { snprintf(lbl,sizeof lbl,"I %s%d",componentTypeLabel(selectedComp->type),selectedComp->id); m_recorder.addChannel(lbl, current_lab::simulation::SignalChannel::Kind::BranchI, selectedComp->id); }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Pin T"))) { snprintf(lbl,sizeof lbl,"T %s%d",componentTypeLabel(selectedComp->type),selectedComp->id); m_recorder.addChannel(lbl, current_lab::simulation::SignalChannel::Kind::ElemT, selectedComp->id); }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Pin P"))) { snprintf(lbl,sizeof lbl,"P %s%d",componentTypeLabel(selectedComp->type),selectedComp->id); m_recorder.addChannel(lbl, current_lab::simulation::SignalChannel::Kind::ElemP, selectedComp->id); }
    }
    if (selectedNode) {
        if (ImGui::SmallButton(tr("Pin V"))) { char lbl[64]; snprintf(lbl,sizeof lbl,"V n%d",selectedNode->id); m_recorder.addChannel(lbl, current_lab::simulation::SignalChannel::Kind::NodeV, selectedNode->id); }
    }
    if (m_recorder.channelCount() > 0) { ImGui::SameLine(); if (ImGui::SmallButton(tr("Clear"))) m_recorder.clear(); }
    auto& chans = m_recorder.channels();
    for (int i = 0; i < (int)chans.size(); ++i) {
        const auto& ch = chans[i];
        float lo = 0.0f, hi = 0.0f; bool first = true; float newest = 0.0f;
        for (int k = 0; k < ch.count; ++k) {
            int idx = (ch.head - ch.count + k + current_lab::simulation::kSignalRingSize) % current_lab::simulation::kSignalRingSize;
            float v = ch.ring[idx];
            if (first) { lo = hi = v; first = false; } else { lo = std::min(lo,v); hi = std::max(hi,v); }
            newest = v;
        }
        if (hi - lo < 1e-6f) { hi += 0.5f; lo -= 0.5f; }
        int offset = ch.count < current_lab::simulation::kSignalRingSize ? 0 : ch.head;
        char overlay[80]; snprintf(overlay, sizeof overlay, "%s = %.3f", ch.label.c_str(), newest);
        ImGui::PushID(i);
        ImGui::PlotLines("##scope", ch.ring, ch.count, offset, overlay, lo, hi, ImVec2(-1, 48));
        if (ImGui::SmallButton(tr("Remove"))) { m_recorder.removeChannel(i); ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (m_recorder.channelCount() == 0)
        ImGui::TextDisabled("%s", tr("Pin a node (V) or element (I/T/P) to scope."));

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
    ImGui::TextDisabled("%s", tr("Live: companion-model MNA, sleeps at steady state"));
    int method = static_cast<int>(m_liveSim.method());
    const char* methods[] = {tr("Backward Euler (stable)"), tr("Trapezoidal (accurate)")};
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::Combo(tr("Method"), &method, methods, IM_ARRAYSIZE(methods)))
        m_liveSim.setMethod(static_cast<IntegrationMethod>(method));
    bool autoSpeed = m_liveSim.autoSpeed();
    if (ImGui::Checkbox(tr("Auto slow-mo"), &autoSpeed)) {
        if (autoSpeed)
            m_liveSim.setAutoSpeed();
        else
            m_liveSim.setManualSpeed(m_manualSimSpeed);
    }
    if (m_liveSim.autoSpeed()) {
        ImGui::TextDisabled(tr("x%.4g sim s / real s (from the circuit's tau)"),
                            m_liveSim.simSpeed());
    } else {
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat(tr("Sim speed"), &m_manualSimSpeed, 1e-5f, 10.0f,
                               "%.5fx sim s / real s", ImGuiSliderFlags_Logarithmic))
            m_liveSim.setManualSpeed(m_manualSimSpeed);
    }
    ImGui::Text("t = %.4f s", m_liveSim.time());
    if (m_liveSim.settled()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", tr("steady"));
    }
    ImGui::Checkbox(tr("Sync cameras"), &m_dualView.syncCameras);
    // Та же единая пауза, что и в топ-баре: останавливает время цепи И визуал.
    ImGui::Checkbox(tr("Pause (circuit time + visuals)"), &m_animationPaused);
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
        // Свежая цепь начинает id с нуля: разряд, чтобы старый заряд не
        // прилип к будущим компонентам с теми же id.
        m_liveSim.discharge(); m_thermal.reset(); resetMechanicsPhases();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset Demo"))) {
        setupTestCircuit();
        m_selNode = -1;
        m_selComp = -1;
        m_liveSim.discharge(); m_thermal.reset(); resetMechanicsPhases();
        circuitEvent();
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
        ImGui::TextWrapped("Live: %s, dt = %.3g ms, t = %.3f s, x%.3g",
                           m_liveSim.method() == IntegrationMethod::BackwardEuler
                               ? "backward Euler" : "trapezoidal",
                           m_liveSim.dt() * 1000.0, m_liveSim.time(), m_liveSim.simSpeed());
        ImGui::TextDisabled("%s", m_liveSim.settled()
                                      ? tr("steady state - solver sleeping")
                                      : tr("transient settling"));
        ImGui::TextWrapped("%s", tr("Lumped circuit + distributed 1D wire"));
        ImGui::TextDisabled("Surface charge: %s", m_showSurfaceCharge ? "heuristic" : "hidden");
        ImGui::TextDisabled("Magnetic: %s", m_showMagnetic ? "qualitative" : "hidden");
        ImGui::TextDisabled("%.0f FPS | sim %.2f ms | panes %.2f ms",
                            ImGui::GetIO().Framerate, m_perfSimMs, m_perfPanesMs);
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
