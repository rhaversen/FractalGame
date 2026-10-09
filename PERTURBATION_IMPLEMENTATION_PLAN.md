# Mandelbulb Perturbation Implementation Plan

**Goal:** Implement true perturbation theory for deep-zoom 3D Mandelbulb rendering using CPU double-precision orbit + GPU float perturbation.

**Status Key:** ❌ Not Started | 🟡 In Progress | ✅ Complete

---

## Phase 1: CPU-Side Derivative Computation

### 1.1 Update Orbit Data Structures ✅

- [x] Add `FVector3d Derivative` field to `FOrbitPoint` struct
- [x] Update `FReferenceOrbit` to validate derivative data
- [x] Modify `ConvertOrbitToFloat` to handle derivatives

**Files:** `MandelbulbOrbitGenerator.h`, `MandelbulbOrbitGenerator.cpp`

### 1.2 Implement 3D Mandelbulb Derivative Calculation ✅

- [x] Compute `dz_{n+1}/dc` using finite-difference Jacobian of the spherical power transform
- [x] Apply recurrence `dz/dc_{n+1} = J_n · dz/dc_n + I`
- [x] Initialize `dz_0/dc = 0` and persist Jacobian per orbit point

**Files:** `MandelbulbOrbitGenerator.cpp`

**Math Reference:**

```text
For z_{n+1} = g_p(z_n) + C:
dz_{n+1}/dc = Jacobian(g_p, z_n) · dz_n/dc + I
where Jacobian is the 3x3 matrix of partial derivatives
```

### 1.3 Switch Orbit Upload To Structured Buffer ✅

- [x] Decide on packing strategy using `FPackedOrbitSample`
- [x] Implement `CreateOrbitBuffer` for RDG structured buffer upload
- [x] Remove legacy texture upload path

**Files:** `FractalSceneViewExtension.cpp`

---

## Phase 2: GPU-Side Perturbation Algorithm

### 2.1 Bind Orbit Structured Buffer ✅

- [x] Replace texture bindings with `StructuredBuffer<FPackedOrbitSample>`
- [x] Provide helpers to load position and apply the Jacobian
- [x] Thread the SRV through `FPerturbationComputeShader::FParameters`
- [x] Upload per-iteration Mandelbulb transform Jacobians (not dz/dc) to match perturbation math

**Files:** `PerturbationShader.h`, `PerturbationShader.usf`

### 2.2 Implement True Perturbation Iteration 🟡

- [x] Replace fallback DE loop with Jacobian-driven recurrence
- [x] Track `epsilon` separately from the reference orbit
- [x] Add on-GPU perturbation diagnostics (epsilon magnitude + breakdown reason)
- [ ] Incorporate higher-order correction or adaptive damping as needed after testing
- [ ] Validate convergence thresholds against deep-zoom scenarios

**Files:** `PerturbationShader.usf`

**Pseudocode:**

```hlsl
float3 epsilon = deltaC;
for (int n = 0; n < maxIter; n++) {
    float3 z_ref = LoadOrbitPoint(n);
    float3 z_actual = z_ref + epsilon;

    if (length(z_actual) > bailout) {
        break;
    }

    float3 z_ref_next = LoadOrbitPoint(n + 1);
    float3 perturbed_next = MandelbulbTransform(z_ref + epsilon, deltaC, power);

    epsilon = perturbed_next - z_ref_next;
}
```

### 2.3 Glitch Detection ❌

- [ ] Monitor `|epsilon|` growth
- [ ] If `|epsilon|` exceeds threshold (e.g., `bailoutRadius * 16`), mark as glitch
- [ ] Return diagnostic color or fallback value

**Files:** `PerturbationShader.usf`

---

## Phase 3: Integration & Optimization

### 3.1 Precision Validation ❌

- [ ] Test at zoom level 1e-3 (baseline float precision)
- [ ] Test at zoom level 1e-6 (requires perturbation)
- [ ] Test at zoom level 1e-9 (deep zoom)
- [ ] Compare CPU-rendered reference image vs GPU perturbation
- [ ] Log precision errors to detect breakdown

**Files:** Testing/validation scripts or Blueprint test harness

### 3.2 Orbit Refresh Logic ❌

- [ ] Implement adaptive thresholds in `ShouldRegenerateOrbit`
- [ ] Tune center drift threshold based on zoom
- [ ] Add user-triggered refresh (Blueprint/C++ function)
- [ ] Monitor orbit generation performance

**Files:** `FractalControlSubsystem.cpp`

### 3.3 Performance Profiling ❌

- [ ] Profile CPU orbit generation time
- [ ] Profile GPU shader execution time
- [ ] Profile texture upload overhead
- [ ] Optimize hot paths if needed
- [ ] Consider async/threaded orbit generation

**Tools:** Unreal Insights, `stat GPU`, `stat unit`

---

## Phase 4: Advanced Features (Optional)

### 4.1 Series Approximation ❌

- [ ] Compute Taylor series coefficients on CPU
- [ ] Upload coefficients to GPU
- [ ] Skip iterations using polynomial evaluation
- [ ] Validate convergence radius

**Impact:** 10-100× speedup for deep zooms

### 4.2 Multi-Reference Orbits ❌

- [ ] Detect glitched regions
- [ ] Generate additional reference orbits for problem areas
- [ ] Stitch together multiple perturbation regions

**Impact:** Eliminate visual artifacts at extreme zooms

### 4.3 Arbitrary Precision CPU Backend ❌

- [ ] Integrate Boost.Multiprecision or similar
- [ ] Support zoom beyond `double` precision limits (< 1e-15)
- [ ] Conditional compilation for extended precision

**Impact:** Enable truly unlimited zoom depth

---

## Testing Checklist

- [ ] Basic rendering works (no perturbation, just orbit display)
- [ ] Derivative calculations do not produce NaN or Inf
- [ ] Texture upload succeeds without corruption
- [ ] Shader compiles without errors
- [ ] Perturbation matches reference at low zoom
- [ ] Perturbation maintains quality at medium zoom (1e-6)
- [ ] Perturbation maintains quality at deep zoom (1e-9+)
- [ ] No visual glitches or artifacts
- [ ] Performance is acceptable (>30 FPS at 1080p)

---

## Current Implementation Status

**Completed:**

- ✅ Basic CPU orbit generation (position + derivatives)
- ✅ Orbit structured buffer upload to GPU
- ✅ Distance estimation shader
- ✅ Ray marching infrastructure
- ✅ View extension integration
- ✅ Parameter subsystem

**In Progress:**

- 🟡 Phase 2.2 – Validate Jacobian-based perturbation loop under deep zooms

**Next Task:**

- ❌ Phase 2.3 – Add glitch detection & fallback coloring

---

## Notes & Decisions

### Texture Packing Decision (Pending)

1. **Two textures:** Clean separation, easy to debug, 2× texture fetches
2. **Wide texture:** Six floats per iteration (position + derivative) in adjacent texels
3. **Stacked rows:** Row 0 contains positions, row 1 contains derivatives

**Recommendation:** Start with two textures for clarity, optimize later if profiling shows pressure.

### Derivative Formula for Mandelbulb

For `z_{n+1} = z_n^p + C` in spherical coordinates, the Jacobian contains mixed trigonometric terms. We must derive the analytic partials or prototype a finite-difference fallback before wiring the shader.

---

## References

- `PERTURBATION_IMPLEMENTATION_NOTES.md`
- `3D_Mandelbulb_Fractal_Definition_and_Iteration.md`
- [Fractal Forums – Perturbation Theory](https://fractalforums.org)
- [Mandelbulb3D Documentation](https://www.mandelbulb.com)

---

**Last Updated:** 2025-11-06  
**Next Review:** After completing Phase 1
