# Handoff: Mechanics Source Tangent Chain

Date: 2026-06-14
Branch: `mechanics-source-tangent-chain`
Worktree: `C:\Users\amidn\electricity-lab-source-sprocket`

## User request

The previous source-drive attempt was rejected because it made the source gear
smaller while the chain still looked physically glued to the gear. The required
concept is a bicycle-chain-like taut chain:

- keep the source gear large;
- route the chain under tension along straight tangent runs;
- let only a few links contact the source sprocket;
- make contacting links tangent to the sprocket pitch circle;
- verify the source sprocket rotates with the moving chain, not against it.

## Implementation

- `src/physics/ChainGeometry.h`
  - adds `driveSprocketPitchRadius`, with a minimum pitch radius of `15.0` so
    the source gear cannot be made smaller than the old visible crank;
  - adds `SourceDrivePath`, built from common external tangents between endpoint
    idler sprockets and the central source drive sprocket;
  - adds `sourceDrivePointAt` for shared sim/render sampling;
  - adds `sourceDriveSprocketPhaseFromChainTravel`, using
    `-chainTravel / pitchRadius` for no-slip tooth motion.

- `src/physics/ChainSim.*`
  - adds `ChainSpec::driveSprocket`;
  - voltage-source loops use `SourceDrivePath` instead of the old oval path.

- `src/projection/ProjectionBuilder.cpp`
  - renders the source as a large toothed drive sprocket;
  - draws tangent guide rails and short source contact arcs;
  - samples fallback links from the same tangent path;
  - keeps plate direction aligned with the actual local chain segment.

- `src/ui/MainWindow.cpp`
  - marks voltage-source chain specs as drive-sprocket loops.

- `src/ui/CircuitCanvas.cpp`
  - expands source hit testing to the large drive-sprocket radius.

- `tests/test_chain_gear.cpp`
  - covers large source gear size, true tangent contact, short source wrap,
    source-specific sim link placement, rendered tangent links, and rotation
    direction.

## Validation

Commands run from the worktree:

```powershell
cmake --build build-ninja --target current-lab-tests
build-ninja\current-lab-tests.exe --gtest_filter=ChainGeometry.*:ChainSimEngagement.*:MechanicsGears.*:MechanicsChain.*:ChainSim.*:MechanicsMapping.*:MechanicsProjection.*:TripleView.*
build-ninja\current-lab-tests.exe
cmake --build build-ninja --target current-lab
```

Results:

- targeted mechanics/chain subset: 41 passed;
- full suite: 476 passed;
- `current-lab.exe` built successfully.

Known unrelated warnings remain in `src/ui/I18n.cpp` and
`src/projection/ProjectionBuilder.cpp` for the existing `\xC2\xB5F` escape.
