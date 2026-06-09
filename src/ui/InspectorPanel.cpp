#include "ui/InspectorPanel.h"
#include "ui/Format.h"
#include <chrono>
#include <ctime>

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
                             int selNode, int selComp) {
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    const Component* selectedComp = nullptr;
    if (selComp >= 0) {
        for (const auto& c : circuit.components) {
            if (c.id == selComp) { selectedComp = &c; break; }
        }
    }
    const Node* selectedNode = nullptr;
    if (selNode >= 0) {
        for (const auto& n : circuit.nodes) {
            if (n.id == selNode) { selectedNode = &n; break; }
        }
    }

    // --- Selected element ---
    if (selectedComp) {
        const char* typeLabel = "?";
        switch (selectedComp->type) {
            case ComponentType::Wire: typeLabel = "Wire"; break;
            case ComponentType::Resistor: typeLabel = "Resistor"; break;
            case ComponentType::VoltageSource: typeLabel = "Voltage Source"; break;
            case ComponentType::Ground: typeLabel = "Ground"; break;
        }
        ImGui::Text("Component: %s", typeLabel);
        ImGui::Text("ID: %d", selectedComp->id);

        // V(x) graph — potential along component
        if (solution && selectedComp->type != ComponentType::Ground) {
            double vA = 0.0, vB = 0.0;
            for (const auto& np : solution->nodePotentials) {
                if (np.nodeId == selectedComp->nodeA) vA = np.potential;
                if (np.nodeId == selectedComp->nodeB) vB = np.potential;
            }

            const int N = 64;
            float values[N];
            double vMin = vA, vMax = vB;
            for (int i = 0; i < N; ++i) {
                double t = (double)i / (N - 1);
                float v = (float)(vA + (vB - vA) * t);
                values[i] = v;
                vMin = std::min(vMin, (double)v);
                vMax = std::max(vMax, (double)v);
            }

            double margin = (vMax - vMin) * 0.15;
            if (margin < 0.01) margin = 0.1;

            char overlay[64];
            std::snprintf(overlay, sizeof(overlay), "%.3f V -> %.3f V", vA, vB);
            ImGui::PlotLines("##vx", values, N, 0, overlay,
                (float)(vMin - margin), (float)(vMax + margin), ImVec2(0, 70));
        }

        if (selectedComp->type == ComponentType::Resistor) {
            Component* mut = circuit.findComponent(selectedComp->id);
            if (mut) {
                double v = mut->value;
                if (ImGui::InputDouble("Resistance (Ohm)", &v, 1.0, 10.0, "%.0f")) {
                    if (v > 0.0) { mut->value = v; if (onChange) onChange(); }
                }
            }
        } else if (selectedComp->type == ComponentType::VoltageSource) {
            Component* mut = circuit.findComponent(selectedComp->id);
            if (mut) {
                double v = mut->value;
                if (ImGui::InputDouble("Voltage (V)", &v, 0.1, 1.0, "%.1f")) {
                    mut->value = v;
                    if (onChange) onChange();
                }
            }
        }
    } else if (selectedNode) {
        ImGui::Text("Node: %s", selectedNode->label.c_str());
        ImGui::Text("ID: %d", selectedNode->id);
        if (solution) {
            for (const auto& np : solution->nodePotentials) {
                if (np.nodeId == selectedNode->id) {
                    ImGui::Text("Potential: %.3f V", np.potential);
                }
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // --- Simulation results ---
    if (solution) {
        ImGui::TextUnformatted("Simulation Results");
        ImGui::Separator();

        for (const auto& np : solution->nodePotentials) {
            const auto& node = circuit.nodes[np.nodeId];
            if (!node.label.empty())
                ImGui::Text("  %s: %.3f V", node.label.c_str(), np.potential);
            else
                ImGui::Text("  Node %d: %.3f V", np.nodeId, np.potential);
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Branch currents:");

        for (const auto& br : solution->branches) {
            const char* typeStr = "?";
            for (const auto& c : circuit.components) {
                if (c.id == br.componentId) {
                    switch (c.type) {
                        case ComponentType::Resistor: typeStr = "R"; break;
                        case ComponentType::VoltageSource: typeStr = "V"; break;
                        case ComponentType::Wire: typeStr = "W"; break;
                        default: break;
                    }
                    break;
                }
            }
            ImGui::Text("  %s%d: I=%.3f mA  dV=%.3f V  P=%.3f mW",
                typeStr, br.componentId,
                milliamps(br.current),
                br.voltageDrop,
                milliwatts(br.power));
        }
    }
}
