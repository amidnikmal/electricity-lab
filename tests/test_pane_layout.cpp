#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "ui/PaneLayout.h"

using namespace current_lab::ui;

TEST(PaneLayoutTree, StartsAsDualCircuitPhysics) {
    PaneLayoutTree tree;
    EXPECT_EQ(tree.paneCount(), 2);
    auto ids = tree.paneIds();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(tree.projectionOf(ids[0]), 0); // Schematic
    EXPECT_EQ(tree.projectionOf(ids[1]), 1); // Physics
}

TEST(PaneLayoutTree, SplitAddsPaneAndInheritsProjection) {
    PaneLayoutTree tree;
    auto ids = tree.paneIds();
    int newId = tree.split(ids[1], true);
    ASSERT_GE(newId, 0);
    EXPECT_EQ(tree.paneCount(), 3);
    EXPECT_EQ(tree.projectionOf(newId), tree.projectionOf(ids[1])); // inherited
    EXPECT_NE(newId, ids[0]);
    EXPECT_NE(newId, ids[1]);
}

TEST(PaneLayoutTree, CloseRemovesPaneAndPromotesSibling) {
    PaneLayoutTree tree;
    auto ids = tree.paneIds();
    int newId = tree.split(ids[1], false);
    ASSERT_EQ(tree.paneCount(), 3);

    EXPECT_TRUE(tree.close(newId));
    EXPECT_EQ(tree.paneCount(), 2);
    EXPECT_EQ(tree.projectionOf(newId), -1); // gone

    EXPECT_TRUE(tree.close(ids[1]));
    EXPECT_EQ(tree.paneCount(), 1);
    EXPECT_FALSE(tree.close(ids[0])); // never close the last pane
    EXPECT_EQ(tree.paneCount(), 1);
}

TEST(PaneLayoutTree, SetProjectionChangesOnlyThatPane) {
    PaneLayoutTree tree;
    auto ids = tree.paneIds();
    EXPECT_TRUE(tree.setProjection(ids[0], 2));
    EXPECT_EQ(tree.projectionOf(ids[0]), 2);
    EXPECT_EQ(tree.projectionOf(ids[1]), 1);
    EXPECT_FALSE(tree.setProjection(9999, 1));
}

TEST(PaneLayoutTree, PresetsRebuildTheTree) {
    PaneLayoutTree tree;
    tree.resetTriple();
    EXPECT_EQ(tree.paneCount(), 3);
    auto ids = tree.paneIds();
    EXPECT_EQ(tree.projectionOf(ids[0]), 0);
    EXPECT_EQ(tree.projectionOf(ids[1]), 1);
    EXPECT_EQ(tree.projectionOf(ids[2]), 2);

    tree.resetSingle(1);
    EXPECT_EQ(tree.paneCount(), 1);
    EXPECT_EQ(tree.projectionOf(tree.paneIds()[0]), 1);
}

TEST(PaneLayoutTree, LayoutTilesTheAreaWithoutOverlap) {
    PaneLayoutTree tree;
    tree.resetTriple();
    auto ids = tree.paneIds();
    tree.split(ids[1], false); // stacked split inside the middle pane

    std::vector<PaneLeafInfo> leaves;
    std::vector<PaneSplitterInfo> splitters;
    tree.layout({0, 0, 1200, 800}, 6.0f, leaves, splitters);

    ASSERT_EQ(static_cast<int>(leaves.size()), tree.paneCount());
    EXPECT_EQ(splitters.size(), leaves.size() - 1); // binary tree invariant

    float totalArea = 0.0f;
    for (const auto& leaf : leaves) {
        EXPECT_GE(leaf.rect.x, -0.5f);
        EXPECT_GE(leaf.rect.y, -0.5f);
        EXPECT_LE(leaf.rect.x + leaf.rect.w, 1200.5f);
        EXPECT_LE(leaf.rect.y + leaf.rect.h, 800.5f);
        EXPECT_GT(leaf.rect.w, 50.0f);
        EXPECT_GT(leaf.rect.h, 50.0f);
        totalArea += leaf.rect.w * leaf.rect.h;
    }
    // Leaves + divider strips together cover the area (no big holes/overlaps).
    EXPECT_GT(totalArea, 1200.0f * 800.0f * 0.9f);
    EXPECT_LT(totalArea, 1200.0f * 800.0f * 1.001f);

    // Pairwise overlap check.
    for (size_t i = 0; i < leaves.size(); ++i) {
        for (size_t j = i + 1; j < leaves.size(); ++j) {
            const auto& a = leaves[i].rect;
            const auto& b = leaves[j].rect;
            bool separated = a.x + a.w <= b.x + 0.5f || b.x + b.w <= a.x + 0.5f ||
                             a.y + a.h <= b.y + 0.5f || b.y + b.h <= a.y + 0.5f;
            EXPECT_TRUE(separated) << "panes " << i << " and " << j << " overlap";
        }
    }
}

TEST(PaneLayoutTree, StackedSplitDividesVertically) {
    PaneLayoutTree tree;
    tree.resetSingle(1);
    int paneId = tree.paneIds()[0];
    tree.split(paneId, /*sideBySide=*/false);

    std::vector<PaneLeafInfo> leaves;
    std::vector<PaneSplitterInfo> splitters;
    tree.layout({0, 0, 1000, 600}, 6.0f, leaves, splitters);

    ASSERT_EQ(leaves.size(), 2u);
    EXPECT_NEAR(leaves[0].rect.x, leaves[1].rect.x, 0.5f); // same column
    EXPECT_LT(leaves[0].rect.y + leaves[0].rect.h, leaves[1].rect.y + 0.5f);
    ASSERT_EQ(splitters.size(), 1u);
    EXPECT_FALSE(splitters[0].sideBySide);
}

TEST(PaneLayoutTree, RatioIsClampedInLayout) {
    PaneLayoutTree tree; // dual with root split
    std::vector<PaneLeafInfo> leaves;
    std::vector<PaneSplitterInfo> splitters;
    tree.layout({0, 0, 1000, 600}, 6.0f, leaves, splitters);
    ASSERT_EQ(splitters.size(), 1u);

    splitters[0].node->ratio = 0.01f; // dragged to the extreme
    tree.layout({0, 0, 1000, 600}, 6.0f, leaves, splitters);
    EXPECT_GE(leaves[0].rect.w, 1000.0f * kPaneMinRatio * 0.9f);
}

// Жёсткая гарантия против лишних скроллбаров: размеры панелей — ЦЕЛЫЕ пиксели и
// не выходят за контейнер даже при дробном входе (как при uiScale=1.75).
namespace {
void expectIntegralAndInside(const PaneLayoutTree& tree, PaneRect area, float gap) {
    std::vector<PaneLeafInfo> leaves;
    std::vector<PaneSplitterInfo> splitters;
    tree.layout(area, gap, leaves, splitters);
    ASSERT_FALSE(leaves.empty());
    float ax1 = std::floor(area.x) + std::floor(area.w);
    float ay1 = std::floor(area.y) + std::floor(area.h);
    for (const auto& lf : leaves) {
        const PaneRect& r = lf.rect;
        EXPECT_EQ(r.x, std::floor(r.x)); EXPECT_EQ(r.y, std::floor(r.y));
        EXPECT_EQ(r.w, std::floor(r.w)); EXPECT_EQ(r.h, std::floor(r.h));
        EXPECT_GE(r.w, 1.0f); EXPECT_GE(r.h, 1.0f);
        EXPECT_LE(r.x + r.w, ax1) << "панель шире контейнера -> скролл";
        EXPECT_LE(r.y + r.h, ay1) << "панель выше контейнера -> скролл";
    }
}
} // namespace

TEST(PaneLayoutTree, IntegralSizesFitContainerEvenWithFractionalInput) {
    PaneLayoutTree triple; triple.resetTriple();
    int mid = triple.paneIds()[1];
    triple.split(mid, /*sideBySide=*/false); // вложенный стек
    expectIntegralAndInside(triple, {0, 0, 1280, 800}, 6.0f);          // целый вход
    expectIntegralAndInside(triple, {0.5f, 0.5f, 1281.6f, 801.4f}, 6.4f); // дробный (uiScale)
    expectIntegralAndInside(triple, {0, 0, 1599.3f, 901.7f}, 7.0f);
}
