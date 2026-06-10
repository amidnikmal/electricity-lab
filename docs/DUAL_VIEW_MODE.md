# Dual View Mode

Date: 2026-06-10

## Purpose

Dual View shows one circuit through two synchronized projections:

- Circuit View: conventional schematic notation.
- Physics View: physical interpretation of the same solved model.

The goal is to connect the symbolic circuit diagram with potential, electric field, current, power, heat, and other physical layers without creating a second independent scene.

## Source Of Truth

`Circuit` is the single source of truth. Each element has one stable `ComponentId`.

Correct data flow:

```text
User action in either view
  -> callback / command
  -> update one Circuit model
  -> solve
  -> rebuild CircuitViewProjection and PhysicsViewProjection
  -> render both panes
```

Forbidden data flow:

```text
Circuit View owns one element collection
Physics View owns another element collection
manual sync between copies
```

Two independent collections would make selection, deletion, editing, and solver results drift apart.

## Projection Links

`ViewLink` stores one `ComponentId` and separate projection bounds for the circuit and physics panes. Bounds are view data only; they do not duplicate the component.

```cpp
struct ViewLink
{
    ComponentId componentId;
    Rect circuitBounds;
    Rect physicsBounds;
};
```

## Selection Sync

`DualViewState::selectedComponentId` is shared by both panes. Selecting a component in Circuit View or Physics View selects the same `ComponentId`, so both panes highlight the same model element and the same editor opens.

## Camera Sync

Dual View stores two cameras:

```cpp
CanvasCamera circuitCamera;
CanvasCamera physicsCamera;
bool syncCameras = true;
```

When `syncCameras` is enabled, pan or zoom in one pane copies that camera to the other pane. When disabled, both panes move independently. The top bar exposes `Sync cameras` and `Fit`.

## Editing

The element editor edits the shared model:

- resistor resistance;
- voltage source voltage;
- distributed wire resistance-per-unit and segment count;
- ground reference action;
- delete component.

After Apply or Delete, the app re-solves and both projections redraw from the same model.

## Physics Layer Status

- Potential: approximate in the lumped + distributed 1D wire model.
- Electric field: approximate, derived from `E ~= -dV/dx` along conductive paths.
- Heat/power: based on solved branch power and dissipated-power filtering.
- Drift particles: educational visualization, not a carrier-speed simulation.
- Surface charge: heuristic/conceptual.
- Magnetic field: qualitative unless replaced by a calibrated model.

## Current Limitations

- Physics View still reuses `CircuitCanvas` with different layer flags; a dedicated primitive renderer is the next step.
- `ViewLink` bounds are simple component bounds.
- Material selection in the first editor pass is UI state only.
- Energy Flow overlay is not implemented yet.
