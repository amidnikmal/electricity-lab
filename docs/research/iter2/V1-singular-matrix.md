# V1: Singular Matrix Silent Handling

**Verdict: CONFIRMED**

## Evidence

### 1. Pivot skip leaves x[col] = b[col] with no error

`src/math/LinearSystem.h:27` — `if (std::abs(M[pivot][col]) < 1e-15) continue;`

When a column has zero (or near-zero < 1e-15) pivot after partial pivoting, the entire Gaussian elimination step is skipped: no row swap, no normalization, no back-substitution cleanup. `x[col]` retains whatever value it held — initially `b[col]` (line 19: `x = b`), or whatever was set by prior elimination steps.

### 2. No singularity status returned

`src/math/LinearSystem.h:17` — return type is bare `std::vector<double>`. No struct, no rank field, no residual norm, no exception thrown. Caller has zero ability to distinguish a valid full-rank solve from a rank-deficient one.

### 3. Caller blindly trusts result

`src/solver/CircuitSolver.cpp:92` — `auto x = sys.solve();` — the solution vector is used immediately to populate `nodePotentials` and compute branch currents (lines 96-133). No check for `x.size()`, no residual computation, no rank verification.

### 4. Floating resistor case (trace)

`tests/test_solver.cpp:62-70` — 3 nodes (0=ground, 1, 2), one 100Ω resistor between nodes 1 and 2. MNA yields 2×2 matrix:
```
A = [[ 0.01, -0.01],
     [-0.01,  0.01]],   b = [0, 0]
```
After `col=0`: row 1 becomes `[0, 0]`. At `col=1`: `|M[1][1]| = 0 < 1e-15` → `continue`. Result: `x = [0, 0]`. Both floating nodes at 0V, zero current — a **plausible but underdetermined** answer, silently accepted. Test at line 219-232 passes because it expects exactly 0V.

### 5. Parallel identical voltage sources (trace)

`tests/test_solver.cpp:401-414` — two 5V sources in parallel between node 1 and ground. MNA yields 3×3 matrix:
```
A = [[0, 1, 1],
     [1, 0, 0],
     [1, 0, 0]],   b = [0, 5, 5]
```
Rows 1 and 2 of A are **identical** (singular). After `col=0`, row 2 becomes `[0, 0, 0]`. At `col=2`: `|M[2][2]| = 0 < 1e-15` → `continue`. `x[2]` remains 0. Result: node 1 = 5V, both source currents = 0A — only **one** of infinitely many valid solutions (the split between sources is undefined). Test passes because it only checks `finite(current)`.

## Reference

SPICE2 original paper on pivot handling in MNA: L.W. Nagel, "SPICE2: A Computer Program to Simulate Semiconductor Circuits" (1975), §3.2 pivot strategy. https://www2.eecs.berkeley.edu/Pubs/TechRpts/1975/ERL-382.pdf
