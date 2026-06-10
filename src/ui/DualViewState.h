#pragma once

#include "ui/CanvasCamera.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace current_lab::ui
{

using ComponentId = int;

enum class DualViewPane
{
    Circuit,
    Physics,
    Spintronics,
};

struct DualViewState
{
    bool syncCameras = true;
    CanvasCamera circuitCamera;
    CanvasCamera physicsCamera;
    CanvasCamera spintronicsCamera;
    std::optional<ComponentId> selectedComponentId;

    CanvasCamera& cameraFor(DualViewPane pane)
    {
        switch (pane) {
            case DualViewPane::Circuit: return circuitCamera;
            case DualViewPane::Physics: return physicsCamera;
            case DualViewPane::Spintronics: return spintronicsCamera;
        }
        return circuitCamera;
    }

    void select(DualViewPane, ComponentId componentId)
    {
        selectedComponentId = componentId;
    }

    void clearSelection()
    {
        selectedComponentId.reset();
    }

    void pan(DualViewPane pane, Vec2 delta)
    {
        cameraFor(pane).pan(delta);
        syncFrom(pane);
    }

    void zoomAt(DualViewPane pane, float factor, Vec2 screenPoint)
    {
        cameraFor(pane).zoomAt(factor, screenPoint);
        syncFrom(pane);
    }

    void syncFrom(DualViewPane pane)
    {
        if (!syncCameras)
            return;
        const CanvasCamera source = cameraFor(pane);
        circuitCamera = source;
        physicsCamera = source;
        spintronicsCamera = source;
    }
};


struct DualViewPaneSplit
{
    float circuitWidth = 0.0f;
    float physicsWidth = 0.0f;
};

struct DualViewLayoutMetrics
{
    bool showInspector = false;
    float inspectorWidth = 0.0f;
    float collapsedInspectorWidth = 0.0f;
    float canvasWidth = 0.0f;
};

constexpr float kMinPaneRatio = 0.12f;

inline float clampPaneRatio(float ratio, float minRatio = kMinPaneRatio,
                            float maxRatio = 1.0f - kMinPaneRatio)
{
    return std::clamp(ratio, minRatio, maxRatio);
}

// ratio = fraction of the usable width given to the first (circuit) pane.
inline DualViewPaneSplit computeDualViewPaneSplit(float availableWidth, float gap,
                                                  float ratio = 0.5f)
{
    DualViewPaneSplit split;
    float usableWidth = std::max(1.0f, availableWidth);
    ratio = clampPaneRatio(ratio);
    split.circuitWidth = std::floor(std::max(120.0f, (usableWidth - gap) * ratio));
    split.physicsWidth = std::max(120.0f, usableWidth - split.circuitWidth - gap);
    return split;
}

struct TripleViewPaneSplit
{
    float circuitWidth = 0.0f;
    float physicsWidth = 0.0f;
    float spintronicsWidth = 0.0f;
};

// ratio1/ratio2 = fractions of the usable width for the first two panes; the
// third takes the rest. Each pane is kept at least kMinPaneRatio wide.
inline TripleViewPaneSplit computeTripleViewPaneSplit(float availableWidth, float gap,
                                                      float ratio1 = 1.0f / 3.0f,
                                                      float ratio2 = 1.0f / 3.0f)
{
    TripleViewPaneSplit split;
    float usableWidth = std::max(1.0f, availableWidth) - 2.0f * gap;
    ratio1 = clampPaneRatio(ratio1, kMinPaneRatio, 1.0f - 2.0f * kMinPaneRatio);
    ratio2 = clampPaneRatio(ratio2, kMinPaneRatio, 1.0f - ratio1 - kMinPaneRatio);
    split.circuitWidth = std::floor(std::max(110.0f, usableWidth * ratio1));
    split.physicsWidth = std::floor(std::max(110.0f, usableWidth * ratio2));
    split.spintronicsWidth = std::max(110.0f, usableWidth - split.circuitWidth -
                                                  split.physicsWidth);
    return split;
}

inline DualViewLayoutMetrics computeDualViewLayout(float remainingWidth,
                                                   float preferredInspectorWidth,
                                                   bool inspectorRequested,
                                                   bool dualViewEnabled,
                                                   float gap)
{
    DualViewLayoutMetrics layout;
    float minDualWidth = dualViewEnabled ? 620.0f : 360.0f;
    layout.showInspector = inspectorRequested && remainingWidth >= minDualWidth + 240.0f + gap;
    layout.inspectorWidth = layout.showInspector
        ? std::min(preferredInspectorWidth, std::max(240.0f, remainingWidth * 0.26f))
        : 0.0f;
    layout.collapsedInspectorWidth = (!layout.showInspector && inspectorRequested) ? 40.0f : 0.0f;
    float reservedInspectorWidth = layout.showInspector ? layout.inspectorWidth : layout.collapsedInspectorWidth;
    layout.canvasWidth = reservedInspectorWidth > 0.0f
        ? std::max(260.0f, remainingWidth - reservedInspectorWidth - gap)
        : remainingWidth;
    return layout;
}

struct ElementEditState
{
    ComponentId componentId = -1;
    bool isOpen = false;
    double pendingValue = 0.0;
    double pendingWireResistancePerUnit = 0.0;
    int pendingDistributedSegments = 1;
    int pendingMaterial = 0;

    void open(ComponentId id, double value, double wireResistancePerUnit, int distributedSegments)
    {
        componentId = id;
        pendingValue = value;
        pendingWireResistancePerUnit = wireResistancePerUnit;
        pendingDistributedSegments = distributedSegments;
        isOpen = true;
    }

    void close()
    {
        isOpen = false;
        componentId = -1;
    }
};

inline bool cameraApproximatelyEqual(const CanvasCamera& a, const CanvasCamera& b, double eps = 1e-6)
{
    return std::abs(a.offset.x - b.offset.x) <= eps &&
           std::abs(a.offset.y - b.offset.y) <= eps &&
           std::abs(a.scale - b.scale) <= eps;
}

} // namespace current_lab::ui
