#include "ui/PaneLayout.h"

#include <algorithm>

namespace current_lab::ui {

namespace {

PaneNode* findLeaf(PaneNode* node, int paneId) {
    if (!node) return nullptr;
    if (node->isLeaf) return node->paneId == paneId ? node : nullptr;
    if (PaneNode* found = findLeaf(node->a.get(), paneId)) return found;
    return findLeaf(node->b.get(), paneId);
}

// Finds the split node whose direct child is the given leaf.
PaneNode* findParent(PaneNode* node, int paneId) {
    if (!node || node->isLeaf) return nullptr;
    auto isChild = [&](PaneNode* child) { return child && child->isLeaf && child->paneId == paneId; };
    if (isChild(node->a.get()) || isChild(node->b.get())) return node;
    if (PaneNode* found = findParent(node->a.get(), paneId)) return found;
    return findParent(node->b.get(), paneId);
}

int countLeaves(const PaneNode* node) {
    if (!node) return 0;
    if (node->isLeaf) return 1;
    return countLeaves(node->a.get()) + countLeaves(node->b.get());
}

void collectIds(const PaneNode* node, std::vector<int>& ids) {
    if (!node) return;
    if (node->isLeaf) { ids.push_back(node->paneId); return; }
    collectIds(node->a.get(), ids);
    collectIds(node->b.get(), ids);
}

void layoutNode(const PaneNode* node, const PaneRect& rect, float gap,
                std::vector<PaneLeafInfo>& leaves,
                std::vector<PaneSplitterInfo>& splitters) {
    if (!node) return;
    if (node->isLeaf) {
        leaves.push_back({node->paneId, node->projection, rect});
        return;
    }

    float ratio = std::clamp(node->ratio, kPaneMinRatio, 1.0f - kPaneMinRatio);
    if (node->sideBySide) {
        float usable = std::max(1.0f, rect.w - gap);
        float firstW = usable * ratio;
        PaneRect first{rect.x, rect.y, firstW, rect.h};
        PaneRect divider{rect.x + firstW, rect.y, gap, rect.h};
        PaneRect second{rect.x + firstW + gap, rect.y, usable - firstW, rect.h};
        splitters.push_back({const_cast<PaneNode*>(node), divider, true, usable});
        layoutNode(node->a.get(), first, gap, leaves, splitters);
        layoutNode(node->b.get(), second, gap, leaves, splitters);
    } else {
        float usable = std::max(1.0f, rect.h - gap);
        float firstH = usable * ratio;
        PaneRect first{rect.x, rect.y, rect.w, firstH};
        PaneRect divider{rect.x, rect.y + firstH, rect.w, gap};
        PaneRect second{rect.x, rect.y + firstH + gap, rect.w, usable - firstH};
        splitters.push_back({const_cast<PaneNode*>(node), divider, false, usable});
        layoutNode(node->a.get(), first, gap, leaves, splitters);
        layoutNode(node->b.get(), second, gap, leaves, splitters);
    }
}

} // namespace

PaneLayoutTree::PaneLayoutTree() { resetDual(); }

std::unique_ptr<PaneNode> PaneLayoutTree::makeLeaf(int projection) {
    auto leaf = std::make_unique<PaneNode>();
    leaf->isLeaf = true;
    leaf->paneId = m_nextId++;
    leaf->projection = projection;
    return leaf;
}

void PaneLayoutTree::resetSingle(int projection) {
    m_root = makeLeaf(projection);
}

void PaneLayoutTree::resetDual() {
    auto split = std::make_unique<PaneNode>();
    split->isLeaf = false;
    split->sideBySide = true;
    split->ratio = 0.5f;
    split->a = makeLeaf(0); // Schematic
    split->b = makeLeaf(1); // Physics
    m_root = std::move(split);
}

void PaneLayoutTree::resetTriple() {
    auto inner = std::make_unique<PaneNode>();
    inner->isLeaf = false;
    inner->sideBySide = true;
    inner->ratio = 0.5f;
    inner->a = makeLeaf(1); // Physics
    inner->b = makeLeaf(2); // Mechanics

    auto root = std::make_unique<PaneNode>();
    root->isLeaf = false;
    root->sideBySide = true;
    root->ratio = 1.0f / 3.0f;
    root->a = makeLeaf(0); // Schematic
    root->b = std::move(inner);
    m_root = std::move(root);
}

int PaneLayoutTree::split(int paneId, bool sideBySide) {
    PaneNode* leaf = findLeaf(m_root.get(), paneId);
    if (!leaf) return -1;

    auto first = makeLeaf(leaf->projection);
    first->paneId = leaf->paneId; // the original keeps its id (and camera)
    m_nextId--;                   // makeLeaf consumed an id we did not need
    auto second = makeLeaf(leaf->projection);
    int newId = second->paneId;

    leaf->isLeaf = false;
    leaf->sideBySide = sideBySide;
    leaf->ratio = 0.5f;
    leaf->a = std::move(first);
    leaf->b = std::move(second);
    leaf->paneId = 0;
    return newId;
}

bool PaneLayoutTree::close(int paneId) {
    if (paneCount() <= 1) return false;
    PaneNode* parent = findParent(m_root.get(), paneId);
    if (!parent) return false;

    std::unique_ptr<PaneNode> sibling =
        (parent->a && parent->a->isLeaf && parent->a->paneId == paneId)
            ? std::move(parent->b)
            : std::move(parent->a);
    *parent = std::move(*sibling); // promote the sibling subtree in place
    return true;
}

int PaneLayoutTree::paneCount() const { return countLeaves(m_root.get()); }

bool PaneLayoutTree::setProjection(int paneId, int projection) {
    PaneNode* leaf = findLeaf(m_root.get(), paneId);
    if (!leaf) return false;
    leaf->projection = projection;
    return true;
}

int PaneLayoutTree::projectionOf(int paneId) const {
    const PaneNode* leaf = findLeaf(const_cast<PaneNode*>(m_root.get()), paneId);
    return leaf ? leaf->projection : -1;
}

void PaneLayoutTree::layout(const PaneRect& area, float gap,
                            std::vector<PaneLeafInfo>& leaves,
                            std::vector<PaneSplitterInfo>& splitters) const {
    leaves.clear();
    splitters.clear();
    layoutNode(m_root.get(), area, gap, leaves, splitters);
}

std::vector<int> PaneLayoutTree::paneIds() const {
    std::vector<int> ids;
    collectIds(m_root.get(), ids);
    return ids;
}

} // namespace current_lab::ui
