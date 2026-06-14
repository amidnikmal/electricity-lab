#include "ui/CanvasInteraction.h"
#include "projection/ElementGeometry.h"

namespace current_lab::ui {

namespace {
constexpr double kHitRadius = 12.0;
}

int hitTestNode(const Circuit& circuit, Vec2 worldPos) {
    for (const auto& n : circuit.nodes) {
        if ((n.position - worldPos).length() < kHitRadius)
            return n.id;
    }
    return -1;
}

int hitTestComponent(const Circuit& circuit, Vec2 worldPos) {
    for (const auto& c : circuit.components) {
        if (c.type == ComponentType::Ground) continue;
        const Node* nodeA = circuit.findNode(c.nodeA);
        const Node* nodeB = circuit.findNode(c.nodeB);
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

int hitTestSwitchToggle(const Circuit& circuit, Vec2 worldPos, double wireThickness) {
    // Ближайший ключ, а не первый по списку: зоны двух параллельных ключей
    // могут перекрываться, клик по рычагу B не должен щёлкать A.
    int best = -1;
    double bestDist = 0.0;
    for (const auto& c : circuit.components) {
        if (c.type != ComponentType::Switch) continue;
        const Node* nodeA = circuit.findNode(c.nodeA);
        const Node* nodeB = circuit.findNode(c.nodeB);
        if (!nodeA || !nodeB) continue;
        auto g = projection::switchGeometry(nodeA->position, nodeB->position);
        if (!g.valid) continue;
        double dist = (worldPos - g.mid).length();
        if (dist > projection::switchToggleRadius(g, wireThickness)) continue;
        if (best < 0 || dist < bestDist) {
            best = c.id;
            bestDist = dist;
        }
    }
    return best;
}

double defaultValueFor(ComponentType type) {
    switch (type) {
        case ComponentType::Resistor: return 1000.0;
        case ComponentType::VoltageSource: return 5.0;
        case ComponentType::Capacitor: return 1e-3; // 1 mF -> tau = 1 s with 1 kOhm
        case ComponentType::Inductor: return 1.0;   // 1 H  -> tau = 0.1 s with 10 Ohm
        case ComponentType::Diode: return 0.0;      // ideal: no forward drop
        case ComponentType::Switch: return 1.0;     // starts closed
        case ComponentType::Wire:
        case ComponentType::Ground: return 0.0;
    }
    return 0.0;
}

void CanvasInteraction::handle(Circuit& circuit, const InteractionInput& input) {
    if (m_mode == EditorMode::Select)
        handleSelect(circuit, input);
    else
        handlePlace(circuit, input);
}

void CanvasInteraction::handleSelect(const Circuit& circuit, const InteractionInput& input) {
    // Хот-зона выключателя выигрывает у выделения (и у узлов внутри зоны):
    // щелчок ключа — это эксперимент, а не редактирование. Выделение и
    // редактор остаются доступны кликом по выводам вне зоны.
    if (m_dragNode < 0 && input.clicked && callbacks.toggleSwitch) {
        int sw = hitTestSwitchToggle(circuit, input.mouseWorld, input.wireThickness);
        if (sw >= 0) {
            callbacks.toggleSwitch(sw);
            return;
        }
    }

    int hitNode = hitTestNode(circuit, input.mouseWorld);
    int hitComp = hitTestComponent(circuit, input.mouseWorld);

    if (m_dragNode < 0) {
        if (input.clicked && hitNode >= 0) {
            m_selNode = hitNode; m_selComp = -1;
            if (callbacks.selectNode) callbacks.selectNode(hitNode);
        } else if (input.clicked && hitComp >= 0) {
            m_selNode = -1; m_selComp = hitComp;
            if (callbacks.selectComponent) callbacks.selectComponent(hitComp);
        } else if (input.clicked && hitNode < 0 && hitComp < 0) {
            m_selNode = -1; m_selComp = -1;
            if (callbacks.deselect) callbacks.deselect();
        }
        if (m_selNode >= 0 && input.dragging) {
            m_dragNode = m_selNode;
        }
    }

    if (m_dragNode >= 0) {
        if (input.dragging) {
            if (callbacks.moveNode) callbacks.moveNode(m_dragNode, input.mouseWorld);
        }
        if (input.released) {
            m_dragNode = -1;
            return;
        }
    }

    if (input.deletePressed) {
        if (m_selNode >= 0 || m_selComp >= 0) {
            if (callbacks.deleteSelected) callbacks.deleteSelected();
            m_selNode = -1; m_selComp = -1;
        }
    }
}

void CanvasInteraction::handlePlace(Circuit& circuit, const InteractionInput& input) {
    if (input.escapePressed) {
        m_placeFromNode = -1;
        return;
    }

    if (m_mode == EditorMode::PlaceNode) {
        if (input.clicked) {
            int hit = hitTestNode(circuit, input.mouseWorld);
            if (hit < 0 && callbacks.placeNode) callbacks.placeNode(input.mouseWorld);
        }
        return;
    }

    if (m_mode == EditorMode::PlaceGround) {
        if (input.clicked && callbacks.createComponent) {
            int hit = hitTestNode(circuit, input.mouseWorld);
            int nodeId = hit >= 0 ? hit : circuit.addNode(input.mouseWorld);
            callbacks.createComponent(nodeId, nodeId, ComponentType::Ground,
                                      defaultValueFor(ComponentType::Ground));
        }
        return;
    }

    ComponentType ctype = ComponentType::Wire;
    if (m_mode == EditorMode::PlaceResistor)      ctype = ComponentType::Resistor;
    if (m_mode == EditorMode::PlaceVoltageSource) ctype = ComponentType::VoltageSource;
    if (m_mode == EditorMode::PlaceCapacitor)     ctype = ComponentType::Capacitor;
    if (m_mode == EditorMode::PlaceInductor)      ctype = ComponentType::Inductor;
    if (m_mode == EditorMode::PlaceDiode)         ctype = ComponentType::Diode;
    if (m_mode == EditorMode::PlaceSwitch)        ctype = ComponentType::Switch;
    if (m_mode == EditorMode::PlaceAcVoltageSource) ctype = ComponentType::AcVoltageSource;

    if (m_placeFromNode < 0 && input.clicked) {
        int hit = hitTestNode(circuit, input.mouseWorld);
        m_placeFromNode = hit >= 0 ? hit : circuit.addNode(input.mouseWorld);
    }
    if (m_placeFromNode >= 0 && input.released) {
        int hit = hitTestNode(circuit, input.mouseWorld);
        if (hit >= 0 && hit != m_placeFromNode) {
            if (callbacks.createComponent)
                callbacks.createComponent(m_placeFromNode, hit, ctype, defaultValueFor(ctype));
        } else if (hit < 0) {
            int newId = circuit.addNode(input.mouseWorld);
            if (callbacks.createComponent)
                callbacks.createComponent(m_placeFromNode, newId, ctype, defaultValueFor(ctype));
        }
        m_placeFromNode = -1;
    }
}

} // namespace current_lab::ui
