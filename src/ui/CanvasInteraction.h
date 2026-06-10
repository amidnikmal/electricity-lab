#pragma once

#include "circuit/Circuit.h"
#include "math/Vec2.h"
#include <functional>

// Editing/selection state machine for a canvas. Consumes a plain input snapshot
// (no ImGui), mutates ONLY the CircuitModel through callbacks. Testable.
namespace current_lab::ui {

struct CanvasCallbacks {
    std::function<void(Vec2)> placeNode;
    std::function<void(int, int, ComponentType, double)> createComponent; // fromId, toId, type, value
    std::function<void(int)> selectNode;
    std::function<void(int)> selectComponent;
    std::function<void()> deselect;
    std::function<void(int, Vec2)> moveNode;
    std::function<void()> deleteSelected;
};

struct InteractionInput {
    Vec2 mouseWorld;
    bool clicked = false;   // left button pressed this frame
    bool released = false;  // left button released this frame
    bool dragging = false;  // left button held & moving
    bool deletePressed = false;
    bool escapePressed = false;
};

int hitTestNode(const Circuit& circuit, Vec2 worldPos);
int hitTestComponent(const Circuit& circuit, Vec2 worldPos);
double defaultValueFor(ComponentType type);

class CanvasInteraction {
public:
    CanvasCallbacks callbacks;

    void handle(Circuit& circuit, const InteractionInput& input);

    void setMode(EditorMode m) {
        if (m_mode != m) {
            m_mode = m;
            m_dragNode = -1;
            m_placeFromNode = -1;
        }
    }
    EditorMode mode() const { return m_mode; }

    void setSelected(int nodeId, int compId) { m_selNode = nodeId; m_selComp = compId; }
    int selectedNode() const { return m_selNode; }
    int selectedComponent() const { return m_selComp; }

    int dragNode() const { return m_dragNode; }
    int placeFromNode() const { return m_placeFromNode; }
    void setDragNode(int id) { m_dragNode = id; }
    void setPlaceFromNode(int id) { m_placeFromNode = id; }

private:
    void handleSelect(const Circuit& circuit, const InteractionInput& input);
    void handlePlace(Circuit& circuit, const InteractionInput& input);

    EditorMode m_mode = EditorMode::Select;
    int m_selNode = -1;
    int m_selComp = -1;
    int m_dragNode = -1;
    int m_placeFromNode = -1;
};

} // namespace current_lab::ui
