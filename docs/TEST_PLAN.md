# Test Plan

## Verified In Existing Built Binary

Observed available test binary:

- `./build/current-lab-tests`
- result: 138 tests / 10 suites / pass

That run reflects the pre-existing build artifact available in the workspace.

## Added In This Pass

Planned source-level additions now cover:

- circuit ID stability after deletion
- non-contiguous node IDs
- distributed source mapping preservation
- resistor power check for the `5 V + 1 kOhm` case
- wire resistance scaling helpers
- current equality through distributed wire segments
- pure field model direction and scaling
- pure drift model sign and bounds
- pure magnetic field scaling and reversal
- pure surface-charge sign progression

## Remaining Gaps

- No renderer snapshot tests yet
- No compile-only smoke tests for future `render/` module extraction
- No validator tests for invalid circuits because `CircuitValidator` is not yet implemented
- No UI automation for presets/tooltips

## Next Testing Step

Introduce a render-primitive intermediate representation and snapshot it as JSON.
That gives deterministic tests without depending on GPU pixel diffs.
