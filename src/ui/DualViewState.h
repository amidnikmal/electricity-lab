#pragma once

#include "ui/CircuitCanvas.h"
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
