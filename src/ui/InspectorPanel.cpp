#include "ui/InspectorPanel.h"
#include "physics/PowerModel.h"
#include "physics/WirePhysics.h"
#include "visualization/VisualizationStatus.h"
#include "ui/Format.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace {

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

// Все 8 типов ComponentType покрыты (баг №2: Capacitor/Inductor/Diode/Switch давали "?")
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

const char* componentModelLabel(const Component& component) {
    switch (component.type) {
        case ComponentType::Wire: return "Distributed 1D wire approximation";
        case ComponentType::Resistor: return "Lumped linear resistor";
        case ComponentType::VoltageSource: return "Ideal voltage source";
        case ComponentType::Ground: return "Reference potential definition";
        case ComponentType::Capacitor: return "Lumped linear capacitor";
        case ComponentType::Inductor: return "Lumped linear inductor";
        case ComponentType::Diode: return "Ideal piecewise-linear diode";
        case ComponentType::Switch: return "Ideal switch (open/closed)";
    }
    return "?";
}

current_lab::visualization::VisualizationStatus layerInfoForComponent(const Component& component) {
    using namespace current_lab::visualization;
    switch (component.type) {
        case ComponentType::Wire:
            return layerStatus(VisualizationLayer::Potential);
        case ComponentType::Resistor:
            return layerStatus(VisualizationLayer::Heat);
        case ComponentType::VoltageSource:
            return layerStatus(VisualizationLayer::Power);
        case ComponentType::Ground:
            return layerStatus(VisualizationLayer::Potential);
        case ComponentType::Capacitor:
            return layerStatus(VisualizationLayer::Potential);
        case ComponentType::Inductor:
            return layerStatus(VisualizationLayer::Potential);
        case ComponentType::Diode:
            return layerStatus(VisualizationLayer::Power);
        case ComponentType::Switch:
            return layerStatus(VisualizationLayer::Potential);
    }
    return layerStatus(VisualizationLayer::Potential);
}

void renderPotentialProfile(const Component& component, double vA, double vB) {
    if (component.type == ComponentType::Ground)
        return;

    const int kSamples = 64;
    float values[kSamples];
    double vMin = vA;
    double vMax = vB;
    for (int i = 0; i < kSamples; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(kSamples - 1);
        float v = static_cast<float>(vA + (vB - vA) * t);
        values[i] = v;
        vMin = std::min(vMin, static_cast<double>(v));
        vMax = std::max(vMax, static_cast<double>(v));
    }

    double margin = (vMax - vMin) * 0.15;
    if (margin < 0.01) margin = 0.1;

    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.3f V -> %.3f V", vA, vB);
    ImGui::PlotLines("Potential profile", values, kSamples, 0, overlay,
                     static_cast<float>(vMin - margin),
                     static_cast<float>(vMax + margin),
                     ImVec2(0, 70));
}

} // namespace

void LogPanel::addMessage(const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto lt = std::localtime(&t);
    char timeBuf[16];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", lt);
    m_messages.push_back(std::string(timeBuf) + "  " + msg);
    if (m_messages.size() > 100) m_messages.erase(m_messages.begin());
}

void LogPanel::render() {
    ImGui::TextUnformatted("Log");
    ImGui::Separator();
    for (const auto& msg : m_messages) {
        ImGui::TextUnformatted(msg.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
        ImGui::SetScrollHereY(1.0f);
}

void InspectorPanel::render(Circuit& circuit, const CircuitSolution* solution,
                            int selNode, int selComp,
                            const DistributedWireParameters& distributedWire,
                            float wireThickness,
                            float animationSpeed,
                            bool electronFlowEnabled) {
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    const Component* selectedComp = circuit.findComponent(selComp);
    const Node* selectedNode = circuit.findNode(selNode);

    if (selectedComp) {
        const Node* nodeA = circuit.findNode(selectedComp->nodeA);
        const Node* nodeB = circuit.findNode(selectedComp->nodeB);
        double vA = potentialFor(solution, selectedComp->nodeA);
        double vB = potentialFor(solution, selectedComp->nodeB);
        const BranchResult* branch = branchFor(solution, selectedComp->id);
        double dV = branch ? branch->voltageDrop : (vA - vB);
        double current = branch ? branch->current : 0.0;
        double power = branch ? branch->power : 0.0;
        double length = (nodeA && nodeB) ? (nodeB->position - nodeA->position).length() : 0.0;
        double distributedR = (selectedComp->type == ComponentType::Wire)
            ? current_lab::physics::wireResistance(length, distributedWire.resistancePerUnit)
            : selectedComp->value;
        double eApprox = length > 1e-9 ? dV / length : 0.0;
        auto layerInfo = layerInfoForComponent(*selectedComp);

        ImGui::Text("Component type: %s", componentTypeLabel(selectedComp->type));
        ImGui::Text("ID: %d", selectedComp->id);
        ImGui::Text("Node A: %d", selectedComp->nodeA);
        ImGui::Text("Node B: %d", selectedComp->nodeB);
        ImGui::Text("Va: %.4f V", vA);
        ImGui::Text("Vb: %.4f V", vB);
        ImGui::Text("dV: %.4f V", dV);
        ImGui::Text("I: %.4f mA", milliamps(current));
        ImGui::Text("P: %.4f mW (%s)", milliwatts(power),
                    current_lab::physics::isSupplyingPower(power) ? "supplied" : "dissipated");

        if (selectedComp->type == ComponentType::Resistor)
            ImGui::Text("R: %.4f Ohm", selectedComp->value);
        if (selectedComp->type == ComponentType::VoltageSource)
            ImGui::Text("Source: %.4f V", selectedComp->value);
        if (selectedComp->type == ComponentType::Wire || selectedComp->type == ComponentType::Resistor) {
            ImGui::Text("Length: %.3f wu", length);
            ImGui::Text("Distributed R: %.4f Ohm", distributedR);
            ImGui::Text("E ~= %.5f V/wu", eApprox);
        }
        if (selectedComp->type == ComponentType::Wire) {
            const char* currentDir = current > 0.0 ? "A -> B" : (current < 0.0 ? "B -> A" : "none");
            const char* electronDir = current > 0.0 ? "B -> A" : (current < 0.0 ? "A -> B" : "none");
            ImGui::Text("Wire width: %.2f wu", wireThickness);
            ImGui::Text("R per unit: %.4f Ohm/wu", distributedWire.resistancePerUnit);
            ImGui::Text("Segments: %d", distributedWire.segmentsPerWire);
            ImGui::Text("Conventional current: %s", currentDir);
            ImGui::Text("Electron drift: %s", electronDir);
            ImGui::Text("Visual convention: %s", electronFlowEnabled ? "electron flow" : "conventional current");
            ImGui::Text("Visual speed multiplier: %.2fx", animationSpeed);
            ImGui::Text("Thermal motion: qualitative");
        }

        ImGui::TextWrapped("Model: %s", componentModelLabel(*selectedComp));
        ImGui::TextWrapped("Visualization status: %s", layerInfo.badge);
        ImGui::TextWrapped("%s", layerInfo.description);
        renderPotentialProfile(*selectedComp, vA, vB);

        if (selectedComp->type == ComponentType::Resistor) {
            Component* mut = circuit.findComponent(selectedComp->id);
            if (mut) {
                double v = mut->value;
                if (ImGui::InputDouble("Resistance (Ohm)", &v, 1.0, 10.0, "%.3f")) {
                    if (v > 0.0) {
                        mut->value = v;
                        if (onChange) onChange();
                    }
                }
            }
        } else if (selectedComp->type == ComponentType::VoltageSource) {
            Component* mut = circuit.findComponent(selectedComp->id);
            if (mut) {
                double v = mut->value;
                if (ImGui::InputDouble("Voltage (V)", &v, 0.1, 1.0, "%.3f")) {
                    mut->value = v;
                    if (onChange) onChange();
                }
            }
        }
    } else if (selectedNode) {
        ImGui::Text("Node: %s", selectedNode->label.empty() ? "(unnamed)" : selectedNode->label.c_str());
        ImGui::Text("ID: %d", selectedNode->id);
        ImGui::Text("Position: (%.2f, %.2f)", selectedNode->position.x, selectedNode->position.y);
        if (solution)
            ImGui::Text("Potential: %.4f V", potentialFor(solution, selectedNode->id));
    } else {
        ImGui::TextDisabled("Select a node or component to inspect its model and solved values.");
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (solution) {
        ImGui::TextUnformatted("Simulation Results");
        ImGui::Separator();

        for (const auto& np : solution->nodePotentials) {
            const Node* node = circuit.findNode(np.nodeId);
            if (node && !node->label.empty())
                ImGui::Text("  %s: %.4f V", node->label.c_str(), np.potential);
            else
                ImGui::Text("  Node %d: %.4f V", np.nodeId, np.potential);
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Branch values:");
        for (const auto& br : solution->branches) {
            const Component* component = circuit.findComponent(br.componentId);
            const char* typeStr = component ? componentTypeLabel(component->type) : "?";
            const char* powerKind = current_lab::physics::isSupplyingPower(br.power) ? "supplied" : "dissipated";
            ImGui::Text("  %s %d: I=%.4f mA  dV=%.4f V  P=%.4f mW (%s)",
                        typeStr, br.componentId, milliamps(br.current), br.voltageDrop,
                        milliwatts(br.power), powerKind);
        }
    }
}
