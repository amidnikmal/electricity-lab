#include "ui/CircuitCanvas.h"
#include "physics/ChainGeometry.h"
#include "render/PrimitiveRenderer.h"
#include "ui/I18n.h"
#include <algorithm>
#include <cmath>

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

current_lab::projection::ViewParams CircuitCanvas::makeViewParams() const {
    current_lab::projection::ViewParams params;
    params.layers = m_layers;
    params.wireThickness = m_wireThickness;
    params.cameraScale = m_camera.scale;
    params.time = m_animationTime;
    params.debugView = m_debugView;
    params.selectedNode = m_interaction.selectedNode();
    params.selectedComponent = m_interaction.selectedComponent();

    Vec2 worldMin = m_camera.screenToWorld(ImVec2(0, 0));
    Vec2 worldMax = m_camera.screenToWorld(ImVec2(m_size.x, m_size.y));
    if (worldMin.x > worldMax.x) std::swap(worldMin.x, worldMax.x);
    if (worldMin.y > worldMax.y) std::swap(worldMin.y, worldMax.y);
    params.viewMin = worldMin;
    params.viewMax = worldMax;
    params.simParticles = m_simParticles;
    params.paddleStates = m_paddleStates;
    params.chainLinks = m_chainLinks;
    params.flowIntegrals = m_flowIntegrals;
    return params;
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

    dl->AddRectFilled(m_origin, ImVec2(m_origin.x + m_size.x, m_origin.y + m_size.y),
                      IM_COL32(17, 22, 28, 255));
    current_lab::render::drawGrid(dl, m_camera, m_origin, m_size);

    auto result = current_lab::projection::buildProjection(m_projection, circuit, solution,
                                                           makeViewParams());
    current_lab::render::drawPrimitives(dl, result.prims, m_camera, m_origin, m_size);

    if (m_interaction.placeFromNode() >= 0) {
        Node* from = circuit.findNode(m_interaction.placeFromNode());
        if (from) {
            Vec2 mouseWorld = toWorld(ImGui::GetMousePos());
            dl->AddLine(toScreen(from->position), toScreen(mouseWorld),
                        IM_COL32(255, 200, 50, 255), 2.0f);
        }
    }

    ImVec2 mouse = ImGui::GetMousePos();
    bool canvasHovered = (mouse.x >= m_origin.x && mouse.x <= m_origin.x + m_size.x &&
                          mouse.y >= m_origin.y && mouse.y <= m_origin.y + m_size.y);
    bool canvasAcceptsInput = canvasHovered && canvasItemHovered && ImGui::IsWindowHovered();

    if (canvasAcceptsInput) {
        if (m_interaction.dragNode() < 0 && m_interaction.placeFromNode() < 0) {
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

    // Hand-crank: in the Mechanics view, dragging around a voltage-source
    // wheel turns it like a dynamo; the angular speed sets the EMF.
    bool cranking = false;
    if (m_projection == current_lab::projection::ProjectionKind::Mechanical && !m_readOnly) {
        Vec2 world = toWorld(mouse);
        auto findCrank = [&](Vec2 p) -> const Component* {
            for (const auto& comp : circuit.components) {
                if (comp.type != ComponentType::VoltageSource) continue;
                const Node* na = circuit.findNode(comp.nodeA);
                const Node* nb = circuit.findNode(comp.nodeB);
                if (!na || !nb) continue;
                Vec2 mid = (na->position + nb->position) * 0.5;
                double rollerR =
                    current_lab::physics::chain_geometry::linkRadius(m_wireThickness);
                double half =
                    current_lab::physics::chain_geometry::chainHalfWidth(m_wireThickness);
                double driveR =
                    current_lab::physics::chain_geometry::driveSprocketPitchRadius(half, rollerR);
                if ((p - mid).length() < std::max(24.0, driveR * 1.25)) return &comp;
            }
            return nullptr;
        };
        auto crankMid = [&](int id) -> Vec2 {
            const Component* comp = circuit.findComponent(id);
            const Node* na = comp ? circuit.findNode(comp->nodeA) : nullptr;
            const Node* nb = comp ? circuit.findNode(comp->nodeB) : nullptr;
            if (!na || !nb) return Vec2();
            return (na->position + nb->position) * 0.5;
        };

        if (m_crankComponent < 0 && canvasAcceptsInput && findCrank(world)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", current_lab::i18n::tr("Drag to crank the dynamo"));
        }
        if (m_crankComponent >= 0)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (m_crankComponent < 0 && canvasAcceptsInput &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (const Component* comp = findCrank(world)) {
                m_crankComponent = comp->id;
                Vec2 rel = world - crankMid(comp->id);
                m_crankLastAngle = std::atan2(rel.y, rel.x);
                if (callbacks.crankBegin) callbacks.crankBegin(comp->id);
            }
        }
        if (m_crankComponent >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                cranking = true;
                Vec2 rel = world - crankMid(m_crankComponent);
                if (rel.length() > 2.0) {
                    double angle = std::atan2(rel.y, rel.x);
                    double delta = angle - m_crankLastAngle;
                    while (delta > 3.14159265) delta -= 2.0 * 3.14159265;
                    while (delta < -3.14159265) delta += 2.0 * 3.14159265;
                    m_crankLastAngle = angle;
                    double omega = delta / std::max(1e-3f, io.DeltaTime);
                    if (callbacks.driveSource)
                        callbacks.driveSource(m_crankComponent, omega);
                }
            } else {
                if (callbacks.crankEnd) callbacks.crankEnd(m_crankComponent);
                m_crankComponent = -1;
            }
        }
    }

    // Хот-зона выключателя: курсор-рука обещает «клик щёлкнет ключ»
    // (не выделит). Та же геометрия, что и в hit-test'е интеракции.
    if (canvasAcceptsInput && !m_readOnly && !cranking &&
        m_interaction.mode() == EditorMode::Select &&
        current_lab::ui::hitTestSwitchToggle(circuit, toWorld(mouse), m_wireThickness) >= 0)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (canvasAcceptsInput && !m_readOnly && !cranking) {
        current_lab::ui::InteractionInput input;
        input.mouseWorld = toWorld(mouse);
        input.clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        input.released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        input.dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        input.deletePressed = ImGui::IsKeyPressed(ImGuiKey_Delete) ||
                              ImGui::IsKeyPressed(ImGuiKey_Backspace);
        input.escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
        input.wireThickness = m_wireThickness;
        m_interaction.handle(circuit, input);
    }
}
