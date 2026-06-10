#pragma once

#include "ui/CircuitCanvas.h"
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
};

struct DualViewState
{
    bool syncCameras = true;
    CanvasCamera circuitCamera;
    CanvasCamera physicsCamera;
    std::optional<ComponentId> selectedComponentId;

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
        CanvasCamera& target = pane == DualViewPane::Circuit ? circuitCamera : physicsCamera;
        target.pan(delta);
        syncFrom(pane);
    }

    void zoomAt(DualViewPane pane, float factor, Vec2 screenPoint)
    {
        CanvasCamera& target = pane == DualViewPane::Circuit ? circuitCamera : physicsCamera;
        target.zoomAt(factor, screenPoint);
        syncFrom(pane);
    }

    void syncFrom(DualViewPane pane)
    {
        if (!syncCameras)
            return;
        if (pane == DualViewPane::Circuit)
            physicsCamera = circuitCamera;
        else
            circuitCamera = physicsCamera;
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

inline DualViewPaneSplit computeDualViewPaneSplit(float availableWidth, float gap)
{
    DualViewPaneSplit split;
    float usableWidth = std::max(1.0f, availableWidth);
    split.circuitWidth = std::floor(std::max(120.0f, (usableWidth - gap) * 0.5f));
    split.physicsWidth = std::max(120.0f, usableWidth - split.circuitWidth - gap);
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
