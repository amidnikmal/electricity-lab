#pragma once

#include <memory>
#include <vector>

// Blender-style recursive pane layout: a binary tree whose leaves are views.
// Any leaf can be split side-by-side or stacked, any leaf (except the last
// one) can be closed; split ratios are draggable. Pure logic, no ImGui.
namespace current_lab::ui {

struct PaneRect {
    float x = 0, y = 0, w = 0, h = 0;
};

struct PaneNode {
    // leaf fields
    bool isLeaf = true;
    int paneId = 0;
    int projection = 0; // ProjectionKind index shown in this pane

    // split fields
    bool sideBySide = true; // true: children left|right, false: top/bottom
    float ratio = 0.5f;     // first child's share of the axis
    std::unique_ptr<PaneNode> a, b;
};

struct PaneLeafInfo {
    int paneId = 0;
    int projection = 0;
    PaneRect rect;
};

struct PaneSplitterInfo {
    PaneNode* node = nullptr; // split node whose ratio this divider drags
    PaneRect rect;            // divider strip in layout coordinates
    bool sideBySide = true;
    float axisExtent = 1.0f;  // parent extent along the drag axis
};

constexpr float kPaneMinRatio = 0.12f;

class PaneLayoutTree {
public:
    PaneLayoutTree(); // starts as Dual: Circuit | Physics

    void resetSingle(int projection);
    void resetDual();   // Circuit | Physics
    void resetTriple(); // Circuit | Physics | Spintronics

    // Splits the leaf in two; the new pane inherits the projection.
    // Returns the new pane id, or -1 if the pane was not found.
    int split(int paneId, bool sideBySide);

    // Removes a leaf, promoting its sibling. Refuses to close the last pane.
    bool close(int paneId);

    int paneCount() const;
    bool setProjection(int paneId, int projection);
    int projectionOf(int paneId) const; // -1 when not found

    void layout(const PaneRect& area, float gap,
                std::vector<PaneLeafInfo>& leaves,
                std::vector<PaneSplitterInfo>& splitters) const;

    std::vector<int> paneIds() const;

private:
    std::unique_ptr<PaneNode> makeLeaf(int projection);

    std::unique_ptr<PaneNode> m_root;
    int m_nextId = 1;
};

} // namespace current_lab::ui
