#include "ui/CircuitCanvas.h"
#include "ui/Format.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <imgui_internal.h>

static constexpr double kHitRadius = 12.0;

static ImU32 potentialColor(double v, double vMin, double vMax) {
    double range = vMax - vMin;
    if (range < 1e-12) return IM_COL32(128, 128, 255, 220);
    double t = std::max(0.0, std::min(1.0, (v - vMin) / range));
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB((float)((1.0 - t) * 0.667), 1.0f, 1.0f, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 220);
}

static ImU32 currentColor(double absI, double maxI) {
    if (maxI < 1e-12) return IM_COL32(80, 80, 255, 220);
    double t = std::min(absI / maxI, 1.0);
    double h = (1.0 - t) * 0.67;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB((float)h, 1.0f, 1.0f, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 220);
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
        Vec2 a = circuit.nodes[c.nodeA].position;
        Vec2 b = circuit.nodes[c.nodeB].position;
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
        if (clicked) {
            int hit = hitTestNode(circuit, world);
            if (hit >= 0 && callbacks.createComponent)
                callbacks.createComponent(hit, hit, ComponentType::Ground, 0.0);
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
            if (callbacks.createComponent) callbacks.createComponent(m_placeFromNode, hit, ctype, 0.0);
        } else if (hit < 0) {
            int newId = circuit.addNode(world);
            if (callbacks.createComponent) callbacks.createComponent(m_placeFromNode, newId, ctype, 0.0);
        }
        m_placeFromNode = -1;
    }
}

void CircuitCanvas::render(Circuit& circuit, const CircuitSolution* solution) {
    ImGuiIO& io = ImGui::GetIO();

    m_origin = ImGui::GetCursorScreenPos();
    m_size = ImGui::GetContentRegionAvail();
    ImGui::Dummy(m_size);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(m_origin, ImVec2(m_origin.x + m_size.x, m_origin.y + m_size.y), IM_COL32(30, 30, 35, 255));
    drawGrid(dl);

    double globalMaxI = 0.0;
    double globalMaxE = 0.0;
    double globalMaxP = 0.0;
    if (solution) {
        for (const auto& br : solution->branches) {
            globalMaxI = std::max(globalMaxI, std::abs(br.current));
            globalMaxP = std::max(globalMaxP, br.power);
            const Component* c = circuit.findComponent(br.componentId);
            if (c && c->type != ComponentType::Ground) {
                if (c->nodeA < (int)circuit.nodes.size() && c->nodeB < (int)circuit.nodes.size()) {
                    double L = (circuit.nodes[c->nodeB].position - circuit.nodes[c->nodeA].position).length();
                    if (L > 1.0)
                        globalMaxE = std::max(globalMaxE, std::abs(br.voltageDrop) / L);
                }
            }
        }
    }

    for (const auto& comp : circuit.components)
        drawComponent(dl, comp, circuit, solution, globalMaxI, globalMaxE, globalMaxP);
    for (const auto& node : circuit.nodes)
        drawNode(dl, node, solution);

    if (solution && m_showPotential) {
        double vMin = 0.0, vMax = 0.0;
        for (const auto& np : solution->nodePotentials) {
            vMin = std::min(vMin, np.potential);
            vMax = std::max(vMax, np.potential);
        }
        drawPotentialLegend(dl, vMin, vMax);
    }

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

    if (canvasHovered && ImGui::IsWindowHovered()) {
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

    if (canvasHovered && !m_readOnly) {
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

    ImU32 color = IM_COL32(50, 50, 55, 255);
    float xEnd = m_origin.x + m_size.x;
    float yEnd = m_origin.y + m_size.y;

    for (float x = m_origin.x + (float)ox; x < xEnd; x += gridSpacing)
        dl->AddLine(ImVec2(x, m_origin.y), ImVec2(x, yEnd), color);
    for (float y = m_origin.y + (float)oy; y < yEnd; y += gridSpacing)
        dl->AddLine(ImVec2(m_origin.x, y), ImVec2(xEnd, y), color);
}

void CircuitCanvas::drawNode(ImDrawList* dl, const Node& node, const CircuitSolution* solution) {
    ImVec2 pos = toScreen(node.position);
    ImU32 fill = (node.id == m_selNode) ? IM_COL32(255, 200, 50, 255) : IM_COL32(200, 200, 200, 255);
    float radius = (node.id == m_selNode) ? 7.0f : 5.0f;

    dl->AddCircleFilled(pos, radius, fill);
    dl->AddCircle(pos, radius + 1.0f, IM_COL32(255, 255, 255, 255), 0, 1.5f);

    if (!node.label.empty())
        dl->AddText(ImVec2(pos.x + 10, pos.y - 18), IM_COL32(200, 200, 200, 255), node.label.c_str());

    if (solution) {
        for (const auto& np : solution->nodePotentials) {
            if (np.nodeId == node.id) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.3f V", np.potential);
                dl->AddText(ImVec2(pos.x + 10, pos.y + 2), IM_COL32(100, 200, 255, 255), buf);
            }
        }
    }
}

void CircuitCanvas::drawComponent(ImDrawList* dl, const Component& comp, const Circuit& circuit, const CircuitSolution* solution, double globalMaxI, double globalMaxE, double globalMaxP) {
    if (comp.nodeA >= (int)circuit.nodes.size() || comp.nodeB >= (int)circuit.nodes.size()) return;

    Vec2 a = circuit.nodes[comp.nodeA].position;
    Vec2 b = circuit.nodes[comp.nodeB].position;

    double va = 0.0, vb = 0.0, vMin = 0.0, vMax = 0.0;
    double branchCurrent = 0.0, branchPower = 0.0;
    if (solution) {
        for (const auto& np : solution->nodePotentials) {
            if (np.nodeId == comp.nodeA) va = np.potential;
            if (np.nodeId == comp.nodeB) vb = np.potential;
        }
        for (const auto& np : solution->nodePotentials) {
            vMin = std::min(vMin, np.potential);
            vMax = std::max(vMax, np.potential);
        }
        if (comp.type != ComponentType::Ground) {
            for (const auto& br : solution->branches) {
                if (br.componentId == comp.id) {
                    branchCurrent = br.current;
                    branchPower = br.power;
                    break;
                }
            }
        }
    }

    switch (comp.type) {
        case ComponentType::Wire:           drawWire(dl, a, b, va, vb, vMin, vMax); break;
        case ComponentType::Resistor:       drawResistor(dl, a, b, comp.value, va, vb, vMin, vMax, branchPower, globalMaxP); break;
        case ComponentType::VoltageSource:  drawVoltageSource(dl, a, b, comp.value, va, vb, vMin, vMax); break;
        case ComponentType::Ground:         drawGround(dl, b); break;
    }

    if (solution && comp.type != ComponentType::Ground) {
        Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
        Vec2 dirr = (b - a).normalized();
        Vec2 perp(-dirr.y, dirr.x);

        if (m_showEField)
            drawEFieldArrows(dl, a, b, va, vb, globalMaxE);

        if (m_showMagnetic && std::abs(branchCurrent) > 1e-12)
            drawMagneticField(dl, a, b, branchCurrent);

        if (m_showCurrent && std::abs(branchCurrent) > 1e-12)
            drawCurrentArrows(dl, a, b, branchCurrent, globalMaxI);

        if (m_showDrift && std::abs(branchCurrent) > 1e-12)
            drawDriftParticles(dl, a, b, branchCurrent, comp.id);

        if (m_showSurfaceCharge)
            drawSurfaceCharge(dl, a, b, va, vb, vMin, vMax);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "I=%.2f mA", milliamps(branchCurrent));
        ImVec2 s = toScreen(mid + perp * 18.0);
        dl->AddText(s, IM_COL32(255, 220, 50, 255), buf);

        if (m_showPower) {
            std::snprintf(buf, sizeof(buf), "P=%.2f mW", milliwatts(branchPower));
            ImVec2 s2 = toScreen(mid + perp * 30.0);
            dl->AddText(s2, IM_COL32(255, 160, 80, 255), buf);
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

    float wireW = wireScreenWidth();

    if (m_showHeat && maxP > 1e-12) {
        double frac = std::min(1.0, power / maxP);
        float heatW = wireW + 4.0f + 8.0f * (float)frac;
        ImU32 heatCol = IM_COL32(
            (int)(180 + 75 * frac),
            (int)(100 - 40 * frac),
            (int)(50 - 30 * frac),
            (int)(60 + 90 * frac));
        dl->AddLine(toScreen(a), toScreen(b), heatCol, heatW);
    }

    double rectW = len * 0.18;
    double rectH = wireThickness() * 2.8;
    Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    Vec2 rectLeft = mid - unit * rectW * 0.5;
    Vec2 rectRight = mid + unit * rectW * 0.5;

    auto drawBar = [&](Vec2 p0, Vec2 p1) {
        Vec2 barDir = p1 - p0;
        double barLen = barDir.length();
        if (barLen < 0.5) return;
        Vec2 barUnit = barDir / barLen;
        Vec2 barPerp(-barUnit.y, barUnit.x);
        float halfB = wireThickness() * 0.5f;
        Vec2 bc1 = p0 + barPerp * halfB;
        Vec2 bc2 = p0 - barPerp * halfB;
        Vec2 bc3 = p1 - barPerp * halfB;
        Vec2 bc4 = p1 + barPerp * halfB;

        ImU32 baseCol = IM_COL32(38, 42, 50, 255);
        ImU32 outlineCol = IM_COL32(120, 180, 120, 255);
        float screenHalfB = halfB * m_camera.scale;

        dl->AddQuadFilled(toScreen(bc1), toScreen(bc2), toScreen(bc3), toScreen(bc4), baseCol);

        float screenW = wireScreenWidth();
        bool hasGrad = m_showPotential && std::abs(vMax - vMin) > 1e-12 && screenW > 1.0f;
        int nSeg = hasGrad ? std::max(2, std::min(500, (int)(barLen * m_camera.scale / 2.5))) : 0;
        if (hasGrad) {
            for (int i = 0; i < nSeg; ++i) {
                double s0 = (double)i / nSeg, s1 = (double)(i + 1) / nSeg;
                double ts = (p0 - a).length() / len + (barLen * s0) / len;
                Vec2 sP0 = p0 + barDir * s0;
                Vec2 sP1 = p0 + barDir * s1;
                ImU32 c = potentialColor(va + (vb - va) * ts, vMin, vMax);
                Vec2 ss0 = sP0 + barPerp * halfB;
                Vec2 ss1 = sP0 - barPerp * halfB;
                Vec2 ss2 = sP1 - barPerp * halfB;
                Vec2 ss3 = sP1 + barPerp * halfB;
                dl->AddQuadFilled(toScreen(ss0), toScreen(ss1), toScreen(ss2), toScreen(ss3), c);
            }
        } else if (screenW > 1.0f) {
            float coreHW = halfB * 0.6f;
            Vec2 cc1 = p0 + barPerp * coreHW;
            Vec2 cc2 = p0 - barPerp * coreHW;
            Vec2 cc3 = p1 - barPerp * coreHW;
            Vec2 cc4 = p1 + barPerp * coreHW;
            dl->AddQuadFilled(toScreen(cc1), toScreen(cc2), toScreen(cc3), toScreen(cc4),
                              IM_COL32(130, 200, 130, 255));
        }

        dl->AddQuad(toScreen(bc1), toScreen(bc2), toScreen(bc3), toScreen(bc4), outlineCol, 1.5f);
        dl->AddCircle(toScreen(p0), screenHalfB, outlineCol, 0, 1.5f);
        dl->AddCircle(toScreen(p1), screenHalfB, outlineCol, 0, 1.5f);
    };

    drawBar(a, rectLeft);
    drawBar(rectRight, b);

    ImVec2 c1 = toScreen(rectLeft + perp * rectH);
    ImVec2 c2 = toScreen(rectLeft - perp * rectH);
    ImVec2 c3 = toScreen(rectRight - perp * rectH);
    ImVec2 c4 = toScreen(rectRight + perp * rectH);

    ImU32 bodyCol = IM_COL32(80, 75, 85, 255);
    if (m_showPotential && std::abs(vMax - vMin) > 1e-12) {
        double vMid = (va + vb) * 0.5;
        ImU32 pc = potentialColor(vMid, vMin, vMax);
        int pr = (pc >> 0) & 0xFF, pg = (pc >> 8) & 0xFF, pb = (pc >> 16) & 0xFF;
        bodyCol = IM_COL32(
            (80 + pr) / 2,
            (75 + pg) / 2,
            (85 + pb) / 2,
            255);
    }
    dl->AddQuadFilled(c1, c2, c3, c4, bodyCol);
    dl->AddQuad(c1, c2, c3, c4, IM_COL32(255, 255, 255, 255), 2.5f);

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

void CircuitCanvas::drawCurrentArrows(ImDrawList* dl, Vec2 a, Vec2 b, double current, double globalMaxI) {
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

    double time = ImGui::GetTime();
    double speed = std::max(absI * 20.0, 5.0);
    double phase = std::fmod(time * speed, arrowSpacing);

    int count = (int)((len - arrowSpacing * 0.5) / arrowSpacing) + 1;
    for (int i = 0; i < count; ++i) {
        double t = arrowSpacing * 0.5 + i * arrowSpacing + phase;
        if (t > len) t -= len;
        if (t < 0.0) t += len;
        Vec2 p(a.x + unit.x * t, a.y + unit.y * t);
        drawArrowHead(dl, p, flowDir, arrowSize, color);
    }
}

void CircuitCanvas::drawEFieldArrows(ImDrawList* dl, Vec2 a, Vec2 b, double va, double vb, double maxE) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    double dV = std::abs(vb - va);
    if (maxE < 1e-12 || dV < 1e-12) return;
    double E = dV / len;
    double frac = std::min(1.0, E / maxE);

    float arrowSize = 4.0f + (float)frac * 5.0f;
    arrowSize /= m_camera.scale;
    ImU32 color = IM_COL32(
        (int)(60 + 155 * frac),
        (int)(140 + 115 * frac),
        (int)(60 + 60 * frac), 200);

    double scale = m_camera.scale;
    double arrowSpacing = 36.0 / scale;
    if (arrowSpacing < 10.0) arrowSpacing = 10.0;
    if (arrowSpacing > 80.0) arrowSpacing = 80.0;

    Vec2 eDir = (va > vb) ? unit : (unit * -1.0);

    float screenW = wireScreenWidth();
    float halfW = wireThickness() * 0.5f;

    int rows = 1;
    if (screenW > 24.0f) rows = std::min(5, std::max(1, (int)(screenW / 20.0f)));

    for (int row = 0; row < rows; ++row) {
        double rowOff = 0.0;
        if (rows > 1)
            rowOff = -halfW * 0.85 + halfW * 1.7 * (double)row / (double)(rows - 1);

        int count = (int)((len - arrowSpacing * 0.5) / arrowSpacing) + 1;
        if (count < 1) count = 1;
        for (int i = 0; i < count; ++i) {
            double t = arrowSpacing * 0.5 + i * arrowSpacing;
            if (t > len) break;
            Vec2 p(a.x + unit.x * t + perp.x * rowOff,
                   a.y + unit.y * t + perp.y * rowOff);
            drawArrowHead(dl, p, eDir, arrowSize, color);
        }
    }
}

void CircuitCanvas::drawMagneticField(ImDrawList* dl, Vec2 a, Vec2 b, double current) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    double absI = std::abs(current);
    double intensity = std::min(1.0, absI * 0.2);

    float ww = wireScreenWidth();
    float hw = ww * 0.5f;
    float baseR = ww * 0.7f;
    float r1 = baseR + hw * 0.3f;
    float r2 = baseR + hw * 0.7f;

    double sampleSpacing = 60.0 / m_camera.scale;
    int N = std::max(2, (int)(len / sampleSpacing) + 1);

    ImU32 col1 = IM_COL32((int)(80 + 120 * intensity), (int)(180 - 60 * intensity), (int)(255 - 30 * intensity), (int)(80 + 100 * intensity));
    ImU32 col2 = IM_COL32((int)(60 + 100 * intensity), (int)(150 - 40 * intensity), (int)(220 - 20 * intensity), (int)(50 + 80 * intensity));

    float scales[2] = {r1, r2};
    ImU32 cols[2] = {col1, col2};

    for (int i = 0; i < N; ++i) {
        double t = (double)i / (N - 1);
        Vec2 pt(a.x + ab.x * t, a.y + ab.y * t);
        ImVec2 sc = toScreen(pt);

        for (int j = 0; j < 2; ++j) {
            float r = scales[j];
            dl->AddCircle(sc, r, cols[j], 32, 1.5f);

            float angle = (float)(ImGui::GetTime() * 2.0 + i * 0.8) + (j == 0 ? 0.0f : 3.14159f);
            float arrowAngle = (current > 0.0) ? -angle : angle;
            float ax = sc.x + r * cosf(arrowAngle);
            float ay = sc.y - r * sinf(arrowAngle);
            float tx = cosf(arrowAngle + 3.14159f * 0.5f);
            float ty = -sinf(arrowAngle + 3.14159f * 0.5f);
            float arrowLen = std::min(8.0f, r * 0.6f);
            float aw = std::min(4.0f, arrowLen * 0.5f);

            ImVec2 tip(ax + tx * arrowLen * 0.6f, ay + ty * arrowLen * 0.6f);
            ImVec2 lw(ax + tx * (-arrowLen * 0.4f) + ty * aw, ay + ty * (-arrowLen * 0.4f) - tx * aw);
            ImVec2 rw(ax + tx * (-arrowLen * 0.4f) - ty * aw, ay + ty * (-arrowLen * 0.4f) + tx * aw);
            dl->AddTriangleFilled(tip, lw, rw, cols[j]);
        }
    }
}

void CircuitCanvas::drawPotentialLegend(ImDrawList* dl, double vMin, double vMax) {
    if (std::abs(vMax - vMin) < 1e-12) return;

    float barX = m_origin.x + m_size.x - 24;
    float barY0 = m_origin.y + 12;
    float barH = std::min(150.0f, m_size.y * 0.4f);
    float barW = 10.0f;

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

    dl->AddText(ImVec2(barX - 4, barY0 + barH + 4), IM_COL32(150, 150, 160, 255), "V");
}

void CircuitCanvas::drawDriftParticles(ImDrawList* dl, Vec2 a, Vec2 b, double current, int compId) {
    double absI = std::abs(current);
    if (absI < 1e-12) return;

    Vec2 dir = b - a;
    double len = dir.length();
    if (len < 1.0) return;
    Vec2 unit = dir / len;
    Vec2 perp(-unit.y, unit.x);

    double time = ImGui::GetTime();

    double driftSign = (current < 0.0) ? -1.0 : 1.0;
    if (m_electronFlow) driftSign = -driftSign;

    double driftSpeed = 0.06 + absI * 0.25;

    float screenR = particleScreenRadius(m_camera.scale);
    ImU32 color = m_electronFlow ? IM_COL32(255, 180, 80, 210)
                                  : IM_COL32(120, 180, 255, 210);

    double volume = len * wireThickness() * wireThickness();
    int N = std::max(12, std::min(1200, (int)(volume / 40.0)));

    float halfW = wireThickness() * 0.5f;
    float maxOff = std::max(0.0f, halfW - screenR / m_camera.scale - 0.3f);

    double phase = fmod(time * driftSpeed, 1.0);

    for (int i = 0; i < N; ++i) {
        double seed = (double)i * 2.718281828 + (double)compId * 1.618033989;
        double baseT = fmod(seed * 0.127, 1.0);
        double y0 = fmod(seed * 0.371, 1.0) * 2.0 * maxOff - maxOff;

        double t = baseT + driftSign * phase;
        if (t > 1.0) t -= 1.0;
        if (t < 0.0) t += 1.0;

        double thx = sin(time * 117.3 + seed * 7.1) * 2.8
                   + cos(time * 89.7 + seed * 11.3) * 2.2
                   + sin(time * 143.1 + seed * 3.7) * 1.5;
        double thy = cos(time * 103.7 + seed * 13.1) * 2.8
                   + sin(time * 127.9 + seed * 5.3) * 2.2
                   + cos(time * 77.1 + seed * 17.3) * 1.5;

        double thermalX = thx / m_camera.scale;
        double thermalY = thy / m_camera.scale;

        double oy = y0 + thermalY;
        if (oy > maxOff) oy = maxOff - (oy - maxOff) * 0.3;
        if (oy < -maxOff) oy = -maxOff + (-maxOff - oy) * 0.3;

        double tx = t + thermalX / len;
        if (tx < 0.0) tx = 0.0;
        if (tx > 1.0) tx = 1.0;

        Vec2 pos(a.x + dir.x * tx + perp.x * oy,
                 a.y + dir.y * tx + perp.y * oy);
        ImVec2 sp = toScreen(pos);

        dl->AddCircleFilled(sp, screenR, color);
    }
}

void CircuitCanvas::drawSurfaceCharge(ImDrawList* dl, Vec2 a, Vec2 b, double va, double vb, double vMin, double vMax) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    float screenW = wireScreenWidth();
    if (screenW < 1.0f) return;

    double dV = vb - va;
    double vAvg = (va + vb) * 0.5;
    double vSwing = std::max(std::abs(dV), 1e-9);
    double globalRange = std::max(vMax - vMin, 1e-9);

    float halfW = wireThickness() * 0.5f;
    float edgeOff = halfW * 0.92f;

    int N = std::max(8, std::min(200, (int)(len * m_camera.scale / 4.0)));
    double segmentLen = len / N;

    for (int i = 0; i <= N; ++i) {
        double t = (double)i / N;
        double v = va + dV * t;
        double sigma = (v - vAvg) / vSwing;
        double absSigma = std::abs(sigma);
        if (absSigma < 0.05) continue;

        double laplacian = 0.0;
        if (i >= 1 && i <= N - 1) {
            double vm1 = va + dV * ((double)(i - 1) / N);
            double vp1 = va + dV * ((double)(i + 1) / N);
            laplacian = (vp1 - 2.0 * v + vm1) / (segmentLen * segmentLen);
        }
        double juncStrength = std::tanh(std::abs(laplacian) * 5000.0);
        double totalStrength = std::min(1.0, absSigma * 1.2 + juncStrength * 0.6);

        Vec2 mid(a.x + unit.x * len * t, a.y + unit.y * len * t);

        float dotR = std::max(1.2f, screenW * 0.07f * (float)totalStrength);
        if (dotR < 1.0f) dotR = 1.0f;
        if (dotR > screenW * 0.13f) dotR = screenW * 0.13f;

        bool posCharge = sigma > 0.0;
        int alpha = (int)(140 * totalStrength + 40);
        if (alpha > 230) alpha = 230;

        ImU32 col = posCharge
            ? IM_COL32(255, (int)(100 - 60 * totalStrength), (int)(80 - 50 * totalStrength), alpha)
            : IM_COL32((int)(80 - 50 * totalStrength), (int)(100 - 60 * totalStrength), 255, alpha);

        Vec2 topPt = mid + perp * edgeOff;
        Vec2 botPt = mid - perp * edgeOff;
        dl->AddCircleFilled(toScreen(topPt), dotR, col, 6);
        dl->AddCircleFilled(toScreen(botPt), dotR, col, 6);
    }
}
