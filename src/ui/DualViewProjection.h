#pragma once

#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "math/Vec2.h"
#include <algorithm>
#include <vector>

namespace current_lab::ui
{

using ComponentId = int;

struct Rect
{
    Vec2 min;
    Vec2 max;

    bool contains(Vec2 p) const
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
};

struct ViewLink
{
    ComponentId componentId = -1;
    Rect circuitBounds;
    Rect physicsBounds;
};

struct ElementProjection
{
    ComponentId componentId = -1;
    ComponentType type = ComponentType::Wire;
    double voltageA = 0.0;
    double voltageB = 0.0;
    double current = 0.0;
    double power = 0.0;
};

struct DualViewProjection
{
    std::vector<ViewLink> links;
    std::vector<ElementProjection> circuitElements;
    std::vector<ElementProjection> physicsElements;
};

inline double projectionPotentialFor(const CircuitSolution* solution, int nodeId)
{
    if (!solution)
        return 0.0;
    for (const auto& np : solution->nodePotentials) {
        if (np.nodeId == nodeId)
            return np.potential;
    }
    return 0.0;
}

inline const BranchResult* projectionBranchFor(const CircuitSolution* solution, ComponentId componentId)
{
    if (!solution)
        return nullptr;
    for (const auto& br : solution->branches) {
        if (br.componentId == componentId)
            return &br;
    }
    return nullptr;
}

inline Rect boundsForComponent(const Circuit& circuit, const Component& component, double padding = 12.0)
{
    const Node* a = circuit.findNode(component.nodeA);
    const Node* b = circuit.findNode(component.nodeB);
    if (!a || !b)
        return {{0, 0}, {0, 0}};

    double minX = std::min(a->position.x, b->position.x) - padding;
    double minY = std::min(a->position.y, b->position.y) - padding;
    double maxX = std::max(a->position.x, b->position.x) + padding;
    double maxY = std::max(a->position.y, b->position.y) + padding;
    return {{minX, minY}, {maxX, maxY}};
}

inline DualViewProjection buildDualViewProjection(const Circuit& circuit, const CircuitSolution* solution)
{
    DualViewProjection projection;
    projection.links.reserve(circuit.components.size());
    projection.circuitElements.reserve(circuit.components.size());
    projection.physicsElements.reserve(circuit.components.size());

    for (const auto& component : circuit.components) {
        Rect bounds = boundsForComponent(circuit, component);
        projection.links.push_back({component.id, bounds, bounds});

        ElementProjection element;
        element.componentId = component.id;
        element.type = component.type;
        element.voltageA = projectionPotentialFor(solution, component.nodeA);
        element.voltageB = projectionPotentialFor(solution, component.nodeB);
        if (const BranchResult* branch = projectionBranchFor(solution, component.id)) {
            element.current = branch->current;
            element.power = branch->power;
        }

        projection.circuitElements.push_back(element);
        projection.physicsElements.push_back(element);
    }

    return projection;
}

inline bool projectionHasComponent(const DualViewProjection& projection, ComponentId componentId)
{
    return std::any_of(projection.links.begin(), projection.links.end(), [&](const ViewLink& link) {
        return link.componentId == componentId;
    });
}

} // namespace current_lab::ui
