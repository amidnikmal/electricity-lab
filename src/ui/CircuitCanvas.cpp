#include "ui/CircuitCanvas.h"
#include "physics/DriftModel.h"
#include "physics/FieldModel.h"
#include "physics/MagneticFieldModel.h"
#include "physics/PowerModel.h"
#include "physics/ResistiveElementModel.h"
#include "physics/SurfaceChargeModel.h"
#include "physics/WirePhysics.h"
#include "ui/Format.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <imgui_internal.h>

namespace {

constexpr double kHitRadius = 12.0;
constexpr double kPi = 3.14159265358979323846;

struct FieldSource {
    Vec2 position;
    double strength = 0.0;
};

const Node* nodeById(const Circuit& circuit, int nodeId) {
    return circuit.findNode(nodeId);
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

ImU32 rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 220) {
    return IM_COL32(r, g, b, a);
}

ImU32 blend(ImU32 a, ImU32 b, double t) {
    auto chan = [](ImU32 c, int shift) { return static_cast<int>((c >> shift) & 0xFF); };
    double u = std::clamp(t, 0.0, 1.0);
    int r = static_cast<int>(chan(a, 0) + (chan(b, 0) - chan(a, 0)) * u);
    int g = static_cast<int>(chan(a, 8) + (chan(b, 8) - chan(a, 8)) * u);
    int bch = static_cast<int>(chan(a, 16) + (chan(b, 16) - chan(a, 16)) * u);
    int alpha = static_cast<int>(chan(a, 24) + (chan(b, 24) - chan(a, 24)) * u);
    return IM_COL32(r, g, bch, alpha);
}

ImU32 withAlpha(ImU32 color, unsigned char alpha) {
    return IM_COL32(
        static_cast<int>((color >> 0) & 0xFF),
        static_cast<int>((color >> 8) & 0xFF),
        static_cast<int>((color >> 16) & 0xFF),
        alpha);
}

ImU32 potentialColor(double v, double vMin, double vMax) {
    double range = vMax - vMin;
    if (range < 1e-12) return rgba(93, 128, 196);
    double t = std::max(0.0, std::min(1.0, (v - vMin) / range));

    const ImU32 stop0 = rgba(49, 78, 130);
    const ImU32 stop1 = rgba(72, 136, 170);
    const ImU32 stop2 = rgba(213, 170, 82);
    const ImU32 stop3 = rgba(211, 92, 68);

    if (t < 0.35) return blend(stop0, stop1, t / 0.35);
    if (t < 0.75) return blend(stop1, stop2, (t - 0.35) / 0.40);
    return blend(stop2, stop3, (t - 0.75) / 0.25);
}

ImU32 currentColor(double absI, double maxI) {
    if (maxI < 1e-12) return rgba(110, 170, 224);
    double t = std::min(absI / maxI, 1.0);
    return blend(rgba(102, 174, 216), rgba(245, 118, 66), t);
}

double defaultValueFor(ComponentType type) {
    switch (type) {
        case ComponentType::Resistor: return 1000.0;
        case ComponentType::VoltageSource: return 5.0;
        case ComponentType::Wire:
        case ComponentType::Ground: return 0.0;
    }
    return 0.0;
}

ImU32 fieldGlowColor(double strength, unsigned char alpha) {
    if (strength >= 0.0)
        return IM_COL32(255, 188, 55, alpha);
    return IM_COL32(72, 166, 255, alpha);
}

Vec2 qualitativeFieldAt(Vec2 p, const std::vector<FieldSource>& sources) {
    Vec2 e;
    for (const auto& source : sources) {
        Vec2 r = p - source.position;
        double r2 = r.x * r.x + r.y * r.y + 450.0;
        double inv = 1.0 / (r2 * std::sqrt(r2));
        e = e + r * (source.strength * inv);
    }
    return e;
}

} // namespace

void CircuitCanvas::fitToCircuit(const Circuit& circuit) {
    if (circuit.nodes.empty() || m_size.x <= 1.0f || m_size.y <= 1.0f)
        return;

    double minX = circuit.nodes.front().position.x;
    double maxX = minX;
    double minY = circuit.nodes.front().position.y;
    double maxY = minY;
    for (const auto& node : circuit.nodes) {
        minX = std::min(minX, node.position.x);
        maxX = std::max(maxX, node.position.x);
        minY = std::min(minY, node.position.y);
        maxY = std::max(maxY, node.position.y);
    }

    double width = std::max(40.0, maxX - minX);
    double height = std::max(40.0, maxY - minY);
    double pad = 90.0;
    double sx = (m_size.x - pad) / width;
    double sy = (m_size.y - pad) / height;
    double scale = std::clamp(std::min(sx, sy), 0.15, 8.0);
    Vec2 center((minX + maxX) * 0.5, (minY + maxY) * 0.5);
    m_camera.scale = static_cast<float>(scale);
    m_camera.offset = Vec2(m_size.x * 0.5 - center.x * scale, m_size.y * 0.5 - center.y * scale);
}

int CircuitCanvas::hitTestNode(const Circuit& circuit, Vec2 worldPos) const {
    for (const auto& n : circuit.nodes) {
        if ((n.position - worldPos).length() < kHitRadius)
            return n.id;
    }
    return -1;
}

int CircuitCanvas::hitTestComponent(const Circuit& circuit, Vec2 worldPos) const {
    for (const auto& c : circuit.components) {
        if (c.type == ComponentType::Ground) continue;
        const Node* nodeA = nodeById(circuit, c.nodeA);
        const Node* nodeB = nodeById(circuit, c.nodeB);
        if (!nodeA || !nodeB) continue;
        Vec2 a = nodeA->position;
        Vec2 b = nodeB->position;
        Vec2 ab = b - a;
        double len = ab.length();
        if (len < 1.0) continue;
        Vec2 dir = ab / len;
        double t = (worldPos.x - a.x) * dir.x + (worldPos.y - a.y) * dir.y;
        if (t < 0.0 || t > len) continue;
        Vec2 proj(a.x + dir.x * t, a.y + dir.y * t);
        if ((worldPos - proj).length() < kHitRadius)
            return c.id;
    }
    return -1;
}

void CircuitCanvas::handleSelectMode(const Circuit& circuit) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mPos = ImGui::GetMousePos();
    Vec2 world = toWorld(mPos);
    bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    int hitNode = hitTestNode(circuit, world);
    int hitComp = hitTestComponent(circuit, world);

    if (m_dragNode < 0) {
        if (clicked && hitNode >= 0) {
            m_selNode = hitNode; m_selComp = -1;
            if (callbacks.selectNode) callbacks.selectNode(hitNode);
        } else if (clicked && hitComp >= 0) {
            m_selNode = -1; m_selComp = hitComp;
            if (callbacks.selectComponent) callbacks.selectComponent(hitComp);
        } else if (clicked && hitNode < 0 && hitComp < 0) {
            m_selNode = -1; m_selComp = -1;
            if (callbacks.deselect) callbacks.deselect();
        }
        if (m_selNode >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_dragNode = m_selNode;
        }
    }

    if (m_dragNode >= 0) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            if (callbacks.moveNode) callbacks.moveNode(m_dragNode, world);
        }
        if (released) {
            m_dragNode = -1;
            return;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (m_selNode >= 0 || m_selComp >= 0) {
            if (callbacks.deleteSelected) callbacks.deleteSelected();
            m_selNode = -1; m_selComp = -1;
        }
    }
}

void CircuitCanvas::handlePlaceMode(Circuit& circuit) {
    ImVec2 mPos = ImGui::GetMousePos();
    Vec2 world = toWorld(mPos);
    bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_placeFromNode = -1;
        return;
    }

    if (m_mode == EditorMode::PlaceNode) {
        if (clicked) {
            int hit = hitTestNode(circuit, world);
            if (hit < 0 && callbacks.placeNode) callbacks.placeNode(world);
        }
        return;
    }

    if (m_mode == EditorMode::PlaceGround) {
        if (clicked && callbacks.createComponent) {
            int hit = hitTestNode(circuit, world);
            int nodeId = hit >= 0 ? hit : circuit.addNode(world);
            callbacks.createComponent(nodeId, nodeId, ComponentType::Ground, defaultValueFor(ComponentType::Ground));
        }
        return;
    }

    ComponentType ctype = ComponentType::Wire;
    if (m_mode == EditorMode::PlaceResistor)     ctype = ComponentType::Resistor;
    if (m_mode == EditorMode::PlaceVoltageSource) ctype = ComponentType::VoltageSource;

    if (m_placeFromNode < 0 && clicked) {
        int hit = hitTestNode(circuit, world);
        if (hit >= 0) {
            m_placeFromNode = hit;
        } else {
            m_placeFromNode = circuit.addNode(world);
        }
    }
    if (m_placeFromNode >= 0 && released) {
        int hit = hitTestNode(circuit, world);
        if (hit >= 0 && hit != m_placeFromNode) {
            if (callbacks.createComponent) callbacks.createComponent(m_placeFromNode, hit, ctype, defaultValueFor(ctype));
        } else if (hit < 0) {
            int newId = circuit.addNode(world);
            if (callbacks.createComponent) callbacks.createComponent(m_placeFromNode, newId, ctype, defaultValueFor(ctype));
        }
        m_placeFromNode = -1;
    }
}

void CircuitCanvas::render(Circuit& circuit, const CircuitSolution* solution) {
    ImGuiIO& io = ImGui::GetIO();
    if (!m_animationPaused)
        m_animationTime += io.DeltaTime * m_animationSpeed;

    m_origin = ImGui::GetCursorScreenPos();
    m_size = ImGui::GetContentRegionAvail();
    ImGui::Dummy(m_size);
    bool canvasItemHovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(m_origin, ImVec2(m_origin.x + m_size.x, m_origin.y + m_size.y), IM_COL32(17, 22, 28, 255));
    drawGrid(dl);

    double globalMaxI = 0.0;
    double globalMaxE = 0.0;
    double globalMaxP = 0.0;
    double vMin = 0.0;
    double vMax = 0.0;
    if (solution) {
        if (!solution->nodePotentials.empty()) {
            vMin = solution->nodePotentials.front().potential;
            vMax = solution->nodePotentials.front().potential;
        }
        for (const auto& np : solution->nodePotentials) {
            vMin = std::min(vMin, np.potential);
            vMax = std::max(vMax, np.potential);
        }

        for (const auto& br : solution->branches) {
            globalMaxI = std::max(globalMaxI, std::abs(br.current));
            if (const Component* c = circuit.findComponent(br.componentId)) {
                globalMaxP = std::max(globalMaxP,
                    current_lab::physics::dissipatedPowerOnly(c->type, br.power));
            }
            const Component* c = circuit.findComponent(br.componentId);
            if (c && c->type != ComponentType::Ground) {
                const Node* nodeA = circuit.findNode(c->nodeA);
                const Node* nodeB = circuit.findNode(c->nodeB);
                if (!nodeA || !nodeB) continue;
                double eMagnitude = (c->type == ComponentType::Resistor)
                    ? current_lab::physics::resistorBodyElectricFieldMagnitude(nodeA->position, nodeB->position,
                                                                              potentialFor(solution, c->nodeA),
                                                                              potentialFor(solution, c->nodeB),
                                                                              wireThickness())
                    : current_lab::physics::electricFieldMagnitude(br.voltageDrop,
                                                                  (nodeB->position - nodeA->position).length());
                globalMaxE = std::max(globalMaxE, eMagnitude);
            }
        }
    }

    if (solution && (m_showEField || m_showPotential))
        drawElectricFieldBackdrop(dl, circuit, solution, vMin, vMax);

    for (const auto& comp : circuit.components)
        drawComponent(dl, comp, circuit, solution, vMin, vMax, globalMaxI, globalMaxE, globalMaxP);
    drawConductorJunctions(dl, circuit, solution, vMin, vMax);
    for (const auto& node : circuit.nodes)
        drawNode(dl, node, solution);

    if (solution && m_showPotential)
        drawPotentialLegend(dl, vMin, vMax);

    if (m_placeFromNode >= 0) {
        Node* from = circuit.findNode(m_placeFromNode);
        if (from) {
            Vec2 mouseWorld = toWorld(ImGui::GetMousePos());
            dl->AddLine(toScreen(from->position), toScreen(mouseWorld), IM_COL32(255, 200, 50, 255), 2.0f);
        }
    }

    ImVec2 mouse = ImGui::GetMousePos();
    bool canvasHovered = (mouse.x >= m_origin.x && mouse.x <= m_origin.x + m_size.x &&
                          mouse.y >= m_origin.y && mouse.y <= m_origin.y + m_size.y);
    bool canvasAcceptsInput = canvasHovered && canvasItemHovered && ImGui::IsWindowHovered();

    if (canvasAcceptsInput) {
        if (m_dragNode < 0 && m_placeFromNode < 0) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
                m_camera.pan(Vec2(d.x, d.y));
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
            }
            float wheel = io.MouseWheel;
            if (wheel != 0.0f) {
                Vec2 pt(mouse.x - m_origin.x, mouse.y - m_origin.y);
                m_camera.zoomAt(1.0f + wheel * 0.1f, pt);
            }
        }
    }

    if (canvasAcceptsInput && !m_readOnly) {
        switch (m_mode) {
            case EditorMode::Select:     handleSelectMode(circuit); break;
            default:                     handlePlaceMode(circuit);  break;
        }
    }
}

// --- Drawing ---

void CircuitCanvas::drawGrid(ImDrawList* dl) {
    float gridSpacing = 50.0f * m_camera.scale;
    if (gridSpacing < 10.0f) gridSpacing = 10.0f;

    double ox = std::fmod(m_camera.offset.x, (double)gridSpacing);
    double oy = std::fmod(m_camera.offset.y, (double)gridSpacing);

    ImU32 color = IM_COL32(33, 48, 58, 255);
    float xEnd = m_origin.x + m_size.x;
    float yEnd = m_origin.y + m_size.y;

    for (float x = m_origin.x + (float)ox; x < xEnd; x += gridSpacing)
        dl->AddLine(ImVec2(x, m_origin.y), ImVec2(x, yEnd), color);
    for (float y = m_origin.y + (float)oy; y < yEnd; y += gridSpacing)
        dl->AddLine(ImVec2(m_origin.x, y), ImVec2(xEnd, y), color);
}

void CircuitCanvas::drawElectricFieldBackdrop(ImDrawList* dl, const Circuit& circuit, const CircuitSolution* solution,
                                              double vMin, double vMax) {
    if (!solution || std::abs(vMax - vMin) < 1e-12)
        return;

    double range = vMax - vMin;
    double vMid = (vMin + vMax) * 0.5;
    float safeScale = std::max(0.05f, m_camera.scale);

    std::vector<FieldSource> sources;
    sources.reserve(circuit.nodes.size());
    for (const auto& node : circuit.nodes) {
        double q = (potentialFor(solution, node.id) - vMid) / range * 2.0;
        if (std::abs(q) > 0.05)
            sources.push_back({node.position, q});
    }

    if (m_showPotential) {
        for (const auto& comp : circuit.components) {
            if (comp.type == ComponentType::Ground) continue;
            const Node* nodeA = nodeById(circuit, comp.nodeA);
            const Node* nodeB = nodeById(circuit, comp.nodeB);
            if (!nodeA || !nodeB) continue;

            Vec2 a = nodeA->position;
            Vec2 b = nodeB->position;
            double va = potentialFor(solution, comp.nodeA);
            double vb = potentialFor(solution, comp.nodeB);
            Vec2 ab = b - a;
            double len = ab.length();
            if (len < 1.0) continue;

            int segments = std::max(8, std::min(80, static_cast<int>(len * m_camera.scale / 18.0)));
            for (int i = 0; i < segments; ++i) {
                double t0 = static_cast<double>(i) / segments;
                double t1 = static_cast<double>(i + 1) / segments;
                Vec2 p0 = a + ab * t0;
                Vec2 p1 = a + ab * t1;
                ImU32 col = potentialColor(va + (vb - va) * t0, vMin, vMax);
                dl->AddLine(toScreen(p0), toScreen(p1), withAlpha(col, 18), wireScreenWidth() * 6.0f);
                dl->AddLine(toScreen(p0), toScreen(p1), withAlpha(col, 28), wireScreenWidth() * 3.0f);
            }
        }
    }

    for (const auto& source : sources) {
        float intensity = static_cast<float>(std::min(1.0, std::abs(source.strength)));
        float radius = std::clamp((52.0f + 48.0f * intensity) * m_camera.scale, 18.0f, 220.0f);
        ImVec2 center = toScreen(source.position);
        ImU32 glow = fieldGlowColor(source.strength, 16);
        dl->AddCircleFilled(center, radius * 1.8f, fieldGlowColor(source.strength, 6), 48);
        dl->AddCircleFilled(center, radius, glow, 48);
        for (int ring = 1; ring <= 4; ++ring) {
            float rr = radius * (0.45f + 0.32f * ring);
            dl->AddCircle(center, rr, fieldGlowColor(source.strength, static_cast<unsigned char>(28 - ring * 4)), 64, 1.0f);
        }
    }

    if (!m_showEField || sources.empty())
        return;

    Vec2 worldMin = toWorld(m_origin);
    Vec2 worldMax = toWorld(ImVec2(m_origin.x + m_size.x, m_origin.y + m_size.y));
    if (worldMin.x > worldMax.x) std::swap(worldMin.x, worldMax.x);
    if (worldMin.y > worldMax.y) std::swap(worldMin.y, worldMax.y);
    double margin = 180.0 / safeScale;
    worldMin = worldMin - Vec2(margin, margin);
    worldMax = worldMax + Vec2(margin, margin);

    bool hasPositive = false;
    for (const auto& source : sources)
        hasPositive = hasPositive || source.strength > 0.05;

    for (const auto& source : sources) {
        if (hasPositive && source.strength <= 0.05)
            continue;

        double traceSign = source.strength >= 0.0 ? 1.0 : -1.0;
        int seedCount = std::clamp(10 + static_cast<int>(std::abs(source.strength) * 8.0), 10, 18);
        double seedRadius = wireThickness() * 2.0 + 12.0 / safeScale;
        double stepLen = std::clamp(10.0 / safeScale, 2.5, 18.0);

        for (int seed = 0; seed < seedCount; ++seed) {
            double angle = (static_cast<double>(seed) + 0.5) / seedCount * kPi * 2.0;
            Vec2 p = source.position + Vec2(std::cos(angle), std::sin(angle)) * seedRadius;
            std::vector<ImVec2> screenPts;
            std::vector<Vec2> worldPts;
            screenPts.reserve(96);
            worldPts.reserve(96);

            for (int step = 0; step < 84; ++step) {
                if (p.x < worldMin.x || p.x > worldMax.x || p.y < worldMin.y || p.y > worldMax.y)
                    break;

                screenPts.push_back(toScreen(p));
                worldPts.push_back(p);

                Vec2 e = qualitativeFieldAt(p, sources);
                double eLen = e.length();
                if (eLen < 1e-9)
                    break;

                Vec2 dir = (e / eLen) * traceSign;
                p = p + dir * stepLen;

                bool reachedSink = false;
                for (const auto& other : sources) {
                    if (&other == &source) continue;
                    if ((p - other.position).length() < seedRadius * 0.75) {
                        reachedSink = true;
                        break;
                    }
                }
                if (reachedSink)
                    break;
            }

            if (screenPts.size() < 5)
                continue;

            ImU32 lineCol = fieldGlowColor(source.strength, 82);
            dl->AddPolyline(screenPts.data(), static_cast<int>(screenPts.size()), fieldGlowColor(source.strength, 18), ImDrawFlags_None, 4.0f);
            dl->AddPolyline(screenPts.data(), static_cast<int>(screenPts.size()), lineCol, ImDrawFlags_None, 1.25f);

            if (worldPts.size() > 12 && seed % 2 == 0) {
                size_t idx = worldPts.size() / 2;
                Vec2 dir = (worldPts[idx + 1] - worldPts[idx - 1]).normalized();
                drawArrowHead(dl, worldPts[idx], dir, 5.0f / safeScale, fieldGlowColor(source.strength, 120));
            }
        }
    }
}

void CircuitCanvas::drawNode(ImDrawList* dl, const Node& node, const CircuitSolution* solution) {
    ImVec2 pos = toScreen(node.position);
    bool selected = (node.id == m_selNode);
    if (!m_debugView && !selected)
        return;

    ImU32 fill = selected ? IM_COL32(255, 200, 50, 255) : IM_COL32(200, 200, 200, 190);
    float radius = selected ? 6.0f : 4.0f;

    dl->AddCircleFilled(pos, radius, fill);
    dl->AddCircle(pos, radius + 1.0f, selected ? IM_COL32(255, 255, 255, 230) : IM_COL32(255, 255, 255, 150), 0, 1.3f);

    if (m_debugView && !node.label.empty())
        dl->AddText(ImVec2(pos.x + 10, pos.y - 18), IM_COL32(200, 200, 200, 210), node.label.c_str());

    if (solution && (m_debugView || (selected && m_showCanvasReadouts))) {
        for (const auto& np : solution->nodePotentials) {
            if (np.nodeId == node.id) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.3f V", np.potential);
                dl->AddText(ImVec2(pos.x + 10, pos.y + 2), IM_COL32(130, 205, 255, 230), buf);
            }
        }
    }
}

void CircuitCanvas::drawComponent(ImDrawList* dl, const Component& comp, const Circuit& circuit, const CircuitSolution* solution,
                                  double vMin, double vMax, double globalMaxI, double globalMaxE, double globalMaxP) {
    const Node* nodeA = nodeById(circuit, comp.nodeA);
    const Node* nodeB = nodeById(circuit, comp.nodeB);
    if (!nodeA || !nodeB) return;

    Vec2 a = nodeA->position;
    Vec2 b = nodeB->position;

    double va = potentialFor(solution, comp.nodeA);
    double vb = potentialFor(solution, comp.nodeB);
    double branchCurrent = 0.0;
    double branchPower = 0.0;
    if (const BranchResult* branch = branchFor(solution, comp.id)) {
        branchCurrent = branch->current;
        branchPower = branch->power;
    }

    switch (comp.type) {
        case ComponentType::Wire:           drawWire(dl, a, b, va, vb, vMin, vMax); break;
        case ComponentType::Resistor:       drawResistor(dl, a, b, comp.value, va, vb, vMin, vMax, branchPower, globalMaxP); break;
        case ComponentType::VoltageSource:  drawVoltageSource(dl, a, b, comp.value, va, vb, vMin, vMax); break;
        case ComponentType::Ground:         drawGround(dl, b); break;
    }

    if (comp.id == m_selComp) {
        Vec2 dir = b - a;
        double len = dir.length();
        if (len > 1.0) {
            Vec2 unit = dir / len;
            Vec2 perp(-unit.y, unit.x);
            double halfWidth = comp.type == ComponentType::Resistor
                ? current_lab::physics::resistorBodyHalfWidth(wireThickness()) + 6.0 / std::max(0.05f, m_camera.scale)
                : wireThickness() * 0.5 + 8.0 / std::max(0.05f, m_camera.scale);
            Vec2 q1 = a + perp * halfWidth;
            Vec2 q2 = a - perp * halfWidth;
            Vec2 q3 = b - perp * halfWidth;
            Vec2 q4 = b + perp * halfWidth;
            dl->AddQuadFilled(toScreen(q1), toScreen(q2), toScreen(q3), toScreen(q4), IM_COL32(255, 210, 90, 20));
            dl->AddQuad(toScreen(q1), toScreen(q2), toScreen(q3), toScreen(q4), IM_COL32(255, 215, 110, 220), 2.0f);
            dl->AddLine(toScreen(a), toScreen(b), IM_COL32(255, 220, 120, 135), 7.0f);
        }
    }

    if (solution && comp.type != ComponentType::Ground) {
        Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
        Vec2 dirr = (b - a).normalized();
        Vec2 perp(-dirr.y, dirr.x);

        if (comp.type == ComponentType::Resistor) {
            auto sections = current_lab::physics::resistorPathSections(a, b, va, vb, wireThickness());
            for (size_t si = 0; si < sections.size(); ++si) {
                const auto& section = sections[si];
                bool isBody = section.material == current_lab::physics::VisualMaterial::ResistiveBody;
                int sectionId = comp.id * 17 + static_cast<int>(si);

                if (m_showEField && isBody)
                    drawEFieldArrows(dl, section.start, section.end,
                                     section.voltageStart, section.voltageEnd,
                                     globalMaxE, section.halfWidth);

                if (m_showCurrent && std::abs(branchCurrent) > 1e-12)
                    drawCurrentArrows(dl, section.start, section.end, branchCurrent,
                                      globalMaxI, section.halfWidth);

                if (m_showDrift && std::abs(branchCurrent) > 1e-12)
                    drawDriftParticles(dl, section.start, section.end, branchCurrent, sectionId,
                                       section.halfWidth * 2.0, section.driftSpeedScale);

                if (m_showSurfaceCharge && isBody)
                    drawSurfaceCharge(dl, section.start, section.end,
                                      section.voltageStart, section.voltageEnd,
                                      vMin, vMax, section.halfWidth * 2.0);
            }
        } else {
            if (m_showEField)
                drawEFieldArrows(dl, a, b, va, vb, globalMaxE);

            if (m_showCurrent && std::abs(branchCurrent) > 1e-12)
                drawCurrentArrows(dl, a, b, branchCurrent, globalMaxI);

            if (m_showDrift && std::abs(branchCurrent) > 1e-12)
                drawDriftParticles(dl, a, b, branchCurrent, comp.id);

            if (m_showSurfaceCharge)
                drawSurfaceCharge(dl, a, b, va, vb, vMin, vMax);
        }

        if (m_showMagnetic && std::abs(branchCurrent) > 1e-12)
            drawMagneticField(dl, a, b, branchCurrent);

        if (m_debugView || (m_showCanvasReadouts && comp.id == m_selComp)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "I=%.2f mA", milliamps(branchCurrent));
            ImVec2 s = toScreen(mid + perp * 18.0);
            dl->AddText(s, IM_COL32(255, 220, 50, 235), buf);

            if (m_showPower) {
                std::snprintf(buf, sizeof(buf), "P=%.2f mW", milliwatts(branchPower));
                ImVec2 s2 = toScreen(mid + perp * 30.0);
                dl->AddText(s2, IM_COL32(255, 160, 80, 235), buf);
            }
        }
    }
}

void CircuitCanvas::drawConductorJunctions(ImDrawList* dl, const Circuit& circuit, const CircuitSolution* solution,
                                           double vMin, double vMax) {
    float radius = wireThickness() * 0.5f * m_camera.scale;
    if (radius <= 1.0f) return;

    const ImU32 baseCol = IM_COL32(38, 42, 50, 255);
    const ImU32 coreCol = IM_COL32(130, 200, 130, 255);
    bool showPotentialFill = solution && m_showPotential && std::abs(vMax - vMin) > 1e-12;

    for (const auto& node : circuit.nodes) {
        int connected = 0;
        for (const auto& comp : circuit.components) {
            if (comp.type == ComponentType::Ground) continue;
            if (comp.nodeA == node.id || comp.nodeB == node.id)
                ++connected;
        }
        if (connected < 2) continue;

        ImVec2 center = toScreen(node.position);
        if (showPotentialFill) {
            ImU32 fill = withAlpha(potentialColor(potentialFor(solution, node.id), vMin, vMax), 255);
            dl->AddCircleFilled(center, radius, fill, 32);
        } else {
            dl->AddCircleFilled(center, radius, baseCol, 32);
            dl->AddCircleFilled(center, radius * 0.6f, coreCol, 32);
        }
    }
}

void CircuitCanvas::drawVoltageSource(ImDrawList* dl, Vec2 a, Vec2 b, double value, double va, double vb, double vMin, double vMax) {
    Vec2 dir = b - a;
    double len = dir.length();
    if (len < 1.0) return;
    Vec2 unit = dir / len;
    Vec2 perp(-unit.y, unit.x);

    double r = 15.0;
    Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    Vec2 leadA = mid - unit * r;
    Vec2 leadB = mid + unit * r;

    auto drawLead = [&](Vec2 p0, Vec2 p1, double t0, double t1) {
        Vec2 ld = p1 - p0;
        double lLen = ld.length();
        if (lLen < 0.5) return;
        Vec2 lUnit = ld / lLen;
        Vec2 lPerp(-lUnit.y, lUnit.x);
        float halfB = wireThickness() * 0.5f;
        Vec2 lc1 = p0 + lPerp * halfB;
        Vec2 lc2 = p0 - lPerp * halfB;
        Vec2 lc3 = p1 - lPerp * halfB;
        Vec2 lc4 = p1 + lPerp * halfB;

        ImU32 baseCol = IM_COL32(38, 42, 50, 255);
        ImU32 outlineCol = IM_COL32(120, 180, 120, 255);
        float screenHalfB = halfB * m_camera.scale;

        dl->AddQuadFilled(toScreen(lc1), toScreen(lc2), toScreen(lc3), toScreen(lc4), baseCol);

        float screenW = wireScreenWidth();
        bool hasGrad = m_showPotential && std::abs(vMax - vMin) > 1e-12 && screenW > 1.0f;
        int nSeg = hasGrad ? std::max(2, std::min(500, (int)(lLen * m_camera.scale / 2.5))) : 0;
        if (hasGrad) {
            for (int i = 0; i < nSeg; ++i) {
                double s0 = (double)i / nSeg, s1 = (double)(i + 1) / nSeg;
                double ts = t0 + (t1 - t0) * s0;
                Vec2 sP0 = p0 + ld * s0;
                Vec2 sP1 = p0 + ld * s1;
                ImU32 c = potentialColor(va + (vb - va) * ts, vMin, vMax);
                Vec2 ss0 = sP0 + lPerp * halfB;
                Vec2 ss1 = sP0 - lPerp * halfB;
                Vec2 ss2 = sP1 - lPerp * halfB;
                Vec2 ss3 = sP1 + lPerp * halfB;
                dl->AddQuadFilled(toScreen(ss0), toScreen(ss1), toScreen(ss2), toScreen(ss3), c);
            }
        } else if (screenW > 1.0f) {
            float coreHW = halfB * 0.6f;
            Vec2 cc1 = p0 + lPerp * coreHW;
            Vec2 cc2 = p0 - lPerp * coreHW;
            Vec2 cc3 = p1 - lPerp * coreHW;
            Vec2 cc4 = p1 + lPerp * coreHW;
            dl->AddQuadFilled(toScreen(cc1), toScreen(cc2), toScreen(cc3), toScreen(cc4),
                              IM_COL32(130, 200, 130, 255));
        }

        dl->AddQuad(toScreen(lc1), toScreen(lc2), toScreen(lc3), toScreen(lc4), outlineCol, 1.5f);
        dl->AddCircle(toScreen(p0), screenHalfB, outlineCol, 0, 1.5f);
        dl->AddCircle(toScreen(p1), screenHalfB, outlineCol, 0, 1.5f);
    };

    drawLead(a, leadA, 0.0, (len - 2 * r) / (2 * len));
    drawLead(leadB, b, (len + 2 * r) / (2 * len), 1.0);

    ImVec2 sc = toScreen(mid);
    float sr = (float)(r * m_camera.scale);
    dl->AddCircle(sc, sr, IM_COL32(255, 255, 255, 255), 0, 2.5f);

    float s = sr * 0.45f;
    dl->AddLine(ImVec2(sc.x - s, sc.y), ImVec2(sc.x + s, sc.y), IM_COL32(255, 100, 100, 255), 2.0f);
    dl->AddLine(ImVec2(sc.x, sc.y - s), ImVec2(sc.x, sc.y + s), IM_COL32(255, 100, 100, 255), 2.0f);
    dl->AddLine(ImVec2(sc.x - s, sc.y + s * 0.5f), ImVec2(sc.x + s, sc.y + s * 0.5f), IM_COL32(100, 100, 255, 255), 2.0f);

    Vec2 lbl = mid + perp * 22.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f V", value);
    dl->AddText(toScreen(lbl), IM_COL32(200, 200, 200, 255), buf);
}

void CircuitCanvas::drawResistor(ImDrawList* dl, Vec2 a, Vec2 b, double value, double va, double vb, double vMin, double vMax, double power, double maxP) {
    Vec2 dir = b - a;
    double len = dir.length();
    if (len < 1.0) return;
    Vec2 unit = dir / len;
    Vec2 perp(-unit.y, unit.x);

    auto sections = current_lab::physics::resistorPathSections(a, b, va, vb, wireThickness());
    if (sections.empty()) return;

    const auto* body = &sections.front();
    for (const auto& section : sections) {
        if (section.material == current_lab::physics::VisualMaterial::ResistiveBody) {
            body = &section;
            break;
        }
    }

    Vec2 rectLeft = body->start;
    Vec2 rectRight = body->end;
    double rectW = (rectRight - rectLeft).length();
    double rectH = body->halfWidth;
    Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);

    auto drawSection = [&](const current_lab::physics::ConductivePathSection& section) {
        Vec2 barDir = section.end - section.start;
        double barLen = barDir.length();
        if (barLen < 0.5) return;
        Vec2 barUnit = barDir / barLen;
        Vec2 barPerp(-barUnit.y, barUnit.x);
        double halfB = section.halfWidth;
        float screenHalfB = static_cast<float>(halfB * m_camera.scale);

        Vec2 bc1 = section.start + barPerp * halfB;
        Vec2 bc2 = section.start - barPerp * halfB;
        Vec2 bc3 = section.end - barPerp * halfB;
        Vec2 bc4 = section.end + barPerp * halfB;

        bool isBody = section.material == current_lab::physics::VisualMaterial::ResistiveBody;
        ImU32 baseCol = isBody ? IM_COL32(66, 64, 60, 255) : IM_COL32(38, 42, 50, 255);
        ImU32 outlineCol = isBody ? IM_COL32(226, 226, 216, 235) : IM_COL32(120, 180, 120, 255);
        dl->AddQuadFilled(toScreen(bc1), toScreen(bc2), toScreen(bc3), toScreen(bc4), baseCol);

        bool hasGrad = m_showPotential && std::abs(vMax - vMin) > 1e-12;
        if (hasGrad) {
            int nSeg = std::max(2, std::min(700, static_cast<int>(barLen * m_camera.scale / 2.2)));
            for (int i = 0; i < nSeg; ++i) {
                double s0 = static_cast<double>(i) / nSeg;
                double s1 = static_cast<double>(i + 1) / nSeg;
                double v = section.voltageStart + (section.voltageEnd - section.voltageStart) * s0;
                ImU32 c = potentialColor(v, vMin, vMax);
                unsigned char alpha = isBody ? 225 : 185;
                Vec2 p0 = section.start + barDir * s0;
                Vec2 p1 = section.start + barDir * s1;
                Vec2 q1 = p0 + barPerp * halfB;
                Vec2 q2 = p0 - barPerp * halfB;
                Vec2 q3 = p1 - barPerp * halfB;
                Vec2 q4 = p1 + barPerp * halfB;
                dl->AddQuadFilled(toScreen(q1), toScreen(q2), toScreen(q3), toScreen(q4), withAlpha(c, alpha));
            }
        } else if (!isBody && wireScreenWidth() > 1.0f) {
            float coreHW = static_cast<float>(halfB * 0.6);
            Vec2 cc1 = section.start + barPerp * coreHW;
            Vec2 cc2 = section.start - barPerp * coreHW;
            Vec2 cc3 = section.end - barPerp * coreHW;
            Vec2 cc4 = section.end + barPerp * coreHW;
            dl->AddQuadFilled(toScreen(cc1), toScreen(cc2), toScreen(cc3), toScreen(cc4),
                              IM_COL32(130, 200, 130, 255));
        }

        dl->AddQuad(toScreen(bc1), toScreen(bc2), toScreen(bc3), toScreen(bc4), outlineCol, isBody ? 2.1f : 1.5f);
        if (!isBody) {
            dl->AddCircle(toScreen(section.start), screenHalfB, outlineCol, 0, 1.4f);
            dl->AddCircle(toScreen(section.end), screenHalfB, outlineCol, 0, 1.4f);
        }
    };

    double frac = current_lab::physics::heatFraction(ComponentType::Resistor, power, maxP);
    if (m_showHeat && frac > 1e-12 && rectW > 0.5) {
        float heatW = static_cast<float>((rectH * 2.0) * m_camera.scale + 4.0 + 9.0 * frac);
        ImU32 heatCol = IM_COL32(
            static_cast<int>(180 + 75 * frac),
            static_cast<int>(100 - 40 * frac),
            static_cast<int>(50 - 30 * frac),
            static_cast<int>(65 + 95 * frac));
        dl->AddLine(toScreen(rectLeft), toScreen(rectRight), heatCol, heatW);
    }

    for (const auto& section : sections)
        drawSection(section);

    Vec2 b1 = rectLeft + perp * rectH;
    Vec2 b2 = rectLeft - perp * rectH;
    Vec2 b3 = rectRight - perp * rectH;
    Vec2 b4 = rectRight + perp * rectH;
    dl->AddLine(toScreen(b1), toScreen(b2), IM_COL32(245, 245, 238, 240), 2.0f);
    dl->AddLine(toScreen(b4), toScreen(b3), IM_COL32(245, 245, 238, 240), 2.0f);

    if (m_showPotential && std::abs(vMax - vMin) > 1e-12) {
        for (int i = 1; i < 5; ++i) {
            double t = static_cast<double>(i) / 5.0;
            Vec2 p0 = rectLeft + (rectRight - rectLeft) * t;
            ImU32 lineCol = withAlpha(potentialColor(va + (vb - va) * t, vMin, vMax), 105);
            dl->AddLine(toScreen(p0 + perp * rectH * 0.92), toScreen(p0 - perp * rectH * 0.92), lineCol, 1.0f);
        }
    }

    Vec2 lbl = mid + perp * (rectH + 8.0f / m_camera.scale);
    char buf[32];
    if (value >= 1000.0) std::snprintf(buf, sizeof(buf), "%.1f kOhm", value / 1000.0);
    else std::snprintf(buf, sizeof(buf), "%.0f Ohm", value);
    dl->AddText(toScreen(lbl), IM_COL32(200, 200, 200, 255), buf);
}

void CircuitCanvas::drawWire(ImDrawList* dl, Vec2 a, Vec2 b, double va, double vb, double vMin, double vMax) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    float halfW = wireThickness() * 0.5f;
    float screenW = wireScreenWidth();
    float screenHW = halfW * m_camera.scale;

    Vec2 c1 = a + perp * halfW;
    Vec2 c2 = a - perp * halfW;
    Vec2 c3 = b - perp * halfW;
    Vec2 c4 = b + perp * halfW;

    ImVec2 sc1 = toScreen(c1);
    ImVec2 sc2 = toScreen(c2);
    ImVec2 sc3 = toScreen(c3);
    ImVec2 sc4 = toScreen(c4);

    ImU32 baseCol = IM_COL32(38, 42, 50, 255);
    ImU32 outlineCol = IM_COL32(120, 180, 120, 255);

    dl->AddQuadFilled(sc1, sc2, sc3, sc4, baseCol);

    bool hasGradient = m_showPotential && std::abs(vMax - vMin) > 1e-12 && screenW > 1.0f;
    int N = hasGradient ? std::max(2, std::min(1000, (int)(len * m_camera.scale / 2.5))) : 0;

    if (hasGradient) {
        float lastAlpha = 0.0f;
        for (int i = 0; i < N; ++i) {
            double t0 = (double)i / N;
            double t1 = (double)(i + 1) / N;
            ImU32 pc = potentialColor(va + (vb - va) * t0, vMin, vMax);
            int aa = (pc >> 24) & 0xFF;
            int ri = (pc >> 0) & 0xFF;
            int gi = (pc >> 8) & 0xFF;
            int bi = (pc >> 16) & 0xFF;
            float thisAlpha = (lastAlpha + (float)aa) * 0.5f;
            lastAlpha = (float)aa;

            Vec2 p0 = a + unit * (len * t0);
            Vec2 p1 = a + unit * (len * t1);

            Vec2 s0 = p0 + perp * halfW;
            Vec2 s1 = p0 - perp * halfW;
            Vec2 s2 = p1 - perp * halfW;
            Vec2 s3 = p1 + perp * halfW;
            dl->AddQuadFilled(toScreen(s0), toScreen(s1), toScreen(s2), toScreen(s3),
                              IM_COL32(ri, gi, bi, (int)thisAlpha));
        }
    } else if (screenW > 1.0f) {
        float coreHW = halfW * 0.6f;
        Vec2 cc1 = a + perp * coreHW;
        Vec2 cc2 = a - perp * coreHW;
        Vec2 cc3 = b - perp * coreHW;
        Vec2 cc4 = b + perp * coreHW;
        dl->AddQuadFilled(toScreen(cc1), toScreen(cc2), toScreen(cc3), toScreen(cc4),
                          IM_COL32(130, 200, 130, 255));
    }

    dl->AddQuad(sc1, sc2, sc3, sc4, outlineCol, 1.5f);
    dl->AddCircle(toScreen(a), screenHW, outlineCol, 0, 1.5f);
    dl->AddCircle(toScreen(b), screenHW, outlineCol, 0, 1.5f);
}

void CircuitCanvas::drawGround(ImDrawList* dl, Vec2 pos) {
    ImVec2 p = toScreen(pos);
    float l1 = 12.0f, l2 = 8.0f, l3 = 5.0f;
    dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x, p.y + 10), IM_COL32(255, 255, 255, 255), 2.0f);
    dl->AddLine(ImVec2(p.x - l1, p.y + 10), ImVec2(p.x + l1, p.y + 10), IM_COL32(255, 255, 255, 255), 2.0f);
    dl->AddLine(ImVec2(p.x - l2, p.y + 16), ImVec2(p.x + l2, p.y + 16), IM_COL32(255, 255, 255, 255), 2.0f);
    dl->AddLine(ImVec2(p.x - l3, p.y + 22), ImVec2(p.x + l3, p.y + 22), IM_COL32(255, 255, 255, 255), 2.0f);
}

void CircuitCanvas::drawArrowHead(ImDrawList* dl, Vec2 pos, Vec2 dir, float size, ImU32 color) {
    Vec2 right(-dir.y, dir.x);
    Vec2 tip = pos + dir * size;
    Vec2 leftWing = pos - dir * size * 0.4f + right * size * 0.45f;
    Vec2 rightWing = pos - dir * size * 0.4f - right * size * 0.45f;
    dl->AddTriangleFilled(toScreen(tip), toScreen(leftWing), toScreen(rightWing), color);
}

void CircuitCanvas::drawCurrentArrows(ImDrawList* dl, Vec2 a, Vec2 b, double current, double globalMaxI, double conductorHalfWidth) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;

    double absI = std::abs(current);
    double scale = m_camera.scale;

    double arrowSpacing = 40.0 / scale;
    if (arrowSpacing < 8.0) arrowSpacing = 8.0;
    if (arrowSpacing > 80.0) arrowSpacing = 80.0;

    float arrowSize = 6.0f;
    if (globalMaxI > 1e-12) {
        arrowSize = 5.0f + (float)(absI / globalMaxI) * 5.0f;
    }
    arrowSize /= m_camera.scale;

    Vec2 flowDir = unit;
    if (current < 0.0) flowDir = flowDir * -1.0;
    if (m_electronFlow) flowDir = flowDir * -1.0;

    ImU32 color = currentColor(absI, globalMaxI);

    double time = m_animationTime;
    double speed = std::max(absI * 20.0, 5.0);
    double phase = std::fmod(time * speed, arrowSpacing);

    Vec2 perp(-unit.y, unit.x);
    double halfWidth = conductorHalfWidth > 0.0 ? conductorHalfWidth : 0.0;
    double screenWidth = halfWidth * 2.0 * m_camera.scale;
    int rows = 1;
    if (screenWidth > 26.0)
        rows = std::clamp(static_cast<int>(screenWidth / 24.0), 1, 4);

    int count = (int)((len - arrowSpacing * 0.5) / arrowSpacing) + 1;
    for (int row = 0; row < rows; ++row) {
        double rowOffset = 0.0;
        if (rows > 1) {
            rowOffset = -halfWidth * 0.68 + halfWidth * 1.36 * static_cast<double>(row) /
                        static_cast<double>(rows - 1);
        }
        for (int i = 0; i < count; ++i) {
            double t = arrowSpacing * 0.5 + i * arrowSpacing + phase;
            if (t > len) t -= len;
            if (t < 0.0) t += len;
            Vec2 p(a.x + unit.x * t, a.y + unit.y * t);
            drawArrowHead(dl, p + perp * rowOffset, flowDir, arrowSize, color);
        }
    }
}

void CircuitCanvas::drawEFieldArrows(ImDrawList* dl, Vec2 a, Vec2 b, double va, double vb, double maxE, double conductorHalfWidth) {
    current_lab::physics::FieldSamplingConfig config;
    config.cameraScale = m_camera.scale;
    config.wireHalfWidth = conductorHalfWidth > 0.0 ? conductorHalfWidth : wireThickness() * 0.5f;
    config.maxMagnitude = maxE;

    auto samples = current_lab::physics::sampleFieldArrows(a, b, va, vb, config);
    if (samples.empty() || maxE < 1e-12) return;

    double E = samples.front().magnitude;
    double frac = std::min(1.0, E / maxE);

    float arrowSize = 4.0f + (float)frac * 5.0f;
    arrowSize /= m_camera.scale;
    ImU32 color = IM_COL32(
        (int)(60 + 155 * frac),
        (int)(140 + 115 * frac),
        (int)(60 + 60 * frac), 200);

    for (const auto& sample : samples)
        drawArrowHead(dl, sample.position, sample.direction, arrowSize, color);
}

void CircuitCanvas::drawMagneticField(ImDrawList* dl, Vec2 a, Vec2 b, double current) {
    current_lab::physics::MagneticFieldSamplingConfig config;
    config.wireThickness = wireThickness();
    config.cameraScale = m_camera.scale;

    auto samples = current_lab::physics::sampleMagneticField(a, b, current, config);
    if (samples.empty()) return;

    double maxMagnitude = 0.0;
    for (const auto& sample : samples)
        maxMagnitude = std::max(maxMagnitude, sample.magnitude);

    for (const auto& sample : samples) {
        double frac = maxMagnitude > 1e-18 ? sample.magnitude / maxMagnitude : 0.0;
        float glyphR = std::max(3.5f, 3.0f + static_cast<float>(frac) * 4.5f);
        ImU32 col = IM_COL32(
            static_cast<int>(80 + 120 * frac),
            static_cast<int>(160 + 50 * frac),
            static_cast<int>(210 + 35 * frac),
            static_cast<int>(110 + 90 * frac));

        ImVec2 sc = toScreen(sample.position);
        dl->AddCircle(sc, glyphR, col, 16, 1.4f);

        if (sample.direction == current_lab::physics::PageDirection::OutOfPage) {
            dl->AddCircleFilled(sc, std::max(1.2f, glyphR * 0.28f), col, 12);
        } else {
            float arm = glyphR * 0.42f;
            dl->AddLine(ImVec2(sc.x - arm, sc.y - arm), ImVec2(sc.x + arm, sc.y + arm), col, 1.5f);
            dl->AddLine(ImVec2(sc.x - arm, sc.y + arm), ImVec2(sc.x + arm, sc.y - arm), col, 1.5f);
        }
    }
}

void CircuitCanvas::drawPotentialLegend(ImDrawList* dl, double vMin, double vMax) {
    if (std::abs(vMax - vMin) < 1e-12) return;

    float barX = m_origin.x + m_size.x - 28;
    float barY0 = m_origin.y + 12;
    float barH = std::min(150.0f, m_size.y * 0.4f);
    float barW = 12.0f;

    dl->AddRectFilled(ImVec2(barX - 1, barY0 - 1), ImVec2(barX + barW + 1, barY0 + barH + 1),
                      IM_COL32(20, 20, 25, 200));
    dl->AddRect(ImVec2(barX - 1, barY0 - 1), ImVec2(barX + barW + 1, barY0 + barH + 1),
                IM_COL32(100, 100, 110, 255));

    int N = 40;
    for (int i = 0; i < N; ++i) {
        double t = 1.0 - (double)i / N;
        float y0 = barY0 + (float)i / N * barH;
        float y1 = barY0 + (float)(i + 1) / N * barH;
        ImU32 col = potentialColor(vMin + t * (vMax - vMin), vMin, vMax);
        dl->AddRectFilled(ImVec2(barX, y0), ImVec2(barX + barW, y1), col);
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f V", vMax);
    dl->AddText(ImVec2(barX + barW + 4, barY0 - 6), IM_COL32(200, 200, 200, 255), buf);

    std::snprintf(buf, sizeof(buf), "%.2f V", (vMin + vMax) * 0.5);
    dl->AddText(ImVec2(barX + barW + 4, barY0 + barH * 0.5f - 6), IM_COL32(180, 180, 180, 255), buf);

    std::snprintf(buf, sizeof(buf), "%.2f V", vMin);
    dl->AddText(ImVec2(barX + barW + 4, barY0 + barH - 6), IM_COL32(200, 200, 200, 255), buf);

    dl->AddText(ImVec2(barX - 6, barY0 - 18), IM_COL32(150, 185, 210, 255), "Potential");
    dl->AddText(ImVec2(barX - 2, barY0 + barH + 4), IM_COL32(150, 150, 160, 255), "V");
}

void CircuitCanvas::drawDriftParticles(ImDrawList* dl, Vec2 a, Vec2 b, double current, int compId, double visualThickness, double driftSpeedScale) {
    current_lab::physics::DriftSamplingConfig config;
    config.wireThickness = visualThickness > 0.0 ? visualThickness : wireThickness();
    config.cameraScale = m_camera.scale;
    config.time = m_animationTime;
    config.visualSpeedMultiplier *= std::clamp(driftSpeedScale, 0.05, 4.0);
    config.componentId = compId;
    config.electronFlowVisualization = m_electronFlow;

    auto particles = current_lab::physics::sampleDriftParticles(a, b, current, config);
    if (particles.empty()) return;

    float screenR = particleScreenRadius(m_camera.scale);
    ImU32 color = m_electronFlow ? IM_COL32(245, 182, 87, 210)
                                 : IM_COL32(120, 180, 255, 210);

    for (const auto& particle : particles)
        dl->AddCircleFilled(toScreen(particle.position), screenR, color);
}

void CircuitCanvas::drawSurfaceCharge(ImDrawList* dl, Vec2 a, Vec2 b, double va, double vb, double vMin, double vMax, double visualThickness) {
    current_lab::physics::SurfaceChargeSamplingConfig config;
    config.wireThickness = visualThickness > 0.0 ? visualThickness : wireThickness();
    config.cameraScale = m_camera.scale;

    auto samples = current_lab::physics::sampleSurfaceCharges(a, b, va, vb, vMin, vMax, config);
    if (samples.empty()) return;

    float screenW = wireScreenWidth();
    for (const auto& sample : samples) {
        float dotR = std::max(1.2f, screenW * 0.07f * static_cast<float>(sample.displayStrength));
        dotR = std::min(dotR, screenW * 0.13f);

        bool positive = sample.signedStrength > 0.0;
        int alpha = static_cast<int>(140 * sample.displayStrength + 40);
        alpha = std::min(alpha, 230);

        ImU32 col = positive
            ? IM_COL32(255, static_cast<int>(100 - 60 * sample.displayStrength),
                       static_cast<int>(80 - 50 * sample.displayStrength), alpha)
            : IM_COL32(static_cast<int>(80 - 50 * sample.displayStrength),
                       static_cast<int>(100 - 60 * sample.displayStrength), 255, alpha);

        dl->AddCircleFilled(toScreen(sample.topPosition), dotR, col, 6);
        dl->AddCircleFilled(toScreen(sample.bottomPosition), dotR, col, 6);
    }
}
