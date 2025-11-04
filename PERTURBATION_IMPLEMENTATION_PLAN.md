# Perturbation Implementation Analysis & Plan

## Executive Summary

The current implementation is a **standard float-precision 3D Mandelbulb distance-estimation ray marcher**. It does **NOT** implement perturbation theory despite the file naming. To achieve true perturbation-based rendering for deep zooms as outlined in `PERTURBATION_IMPLEMENTATION_NOTES.md`, we need to add a complete CPU→GPU pipeline for high-precision reference orbit generation and GPU-side perturbation math.

---

## Current State Analysis

### What Works (Standard DE Ray Marching)
1. **Scene View Extension Integration** (`FractalSceneViewExtension`)
   - ✅ Correctly hooks into post-tonemapping pass
   - ✅ Dispatches compute shader every frame
   - ✅ Passes camera matrices, viewport params to GPU
   - ✅ Blends fractal output with scene color

2. **Shader Implementation** (`PerturbationShader.usf`)
   - ✅ Classic Mandelbulb formula in spherical coordinates
   - ✅ Distance estimation with derivative tracking
   - ✅ Ray marching with adaptive precision thresholds
   - ✅ Proper camera ray generation from screen space
   - ✅ Color gradient shading based on iteration/step counts

3. **Parameter System** (`FFractalParameter`, `UFractalControlSubsystem`)
   - ✅ Blueprint-accessible controls
   - ✅ Thread-safe parameter updates (mutex protection)
   - ✅ All standard DE parameters exposed (power, bailout, iterations, etc.)

### What's Missing (Perturbation Pipeline)

#### 1. **CPU-Side High-Precision Orbit Generation** ❌
**Status:** Not implemented at all

**Required Components:**
- **Arbitrary/Extended Precision Math**
  - Need `long double` (80-bit) or multiprecision library (Boost.Multiprecision, GMP)
  - Must support complex arithmetic: add, multiply, magnitude squared
  - For 3D Mandelbulb: vector3 with extended precision

- **Reference Orbit Computer**
  - Choose reference point `C_ref` (typically viewport center in fractal space)
  - Iterate Mandelbulb formula `z_{n+1} = g_p(z_n) + C_ref` in high precision
  - Store orbit sequence: `z_0, z_1, ..., z_N` where `N = MaxIterations`
  - Continue until bailout or max iterations

- **Derivative Tracking**
  - Compute `dz/dc` alongside orbit: `dz_{n+1}/dc = ∂g_p/∂z · dz_n/dc`
  - For Mandelbulb power law, derivative requires Jacobian computation
  - Store derivative sequence: `dzdc_0, dzdc_1, ..., dzdc_N`

- **Orbit Refresh Logic**
  - Detect when camera moves beyond perturbation validity threshold
  - Trigger recompute when:
    - `|new_center - C_ref| > scale * threshold`
    - Zoom changes significantly (e.g., factor of 2)
    - MaxIterations increased
  - Run on background thread to avoid hitching

**Implementation Location:**
- New class: `FFractalOrbitGenerator` or `FPerturbationOrbitComputer`
- Lives in `FractalControlSubsystem` or separate service
- Outputs: Float arrays for GPU upload

---

#### 2. **GPU-Side Orbit Data Structures** ❌
**Status:** No buffers allocated or bound

**Required Components:**

**Option A: Texture-Based Storage**
```cpp
// In FPerturbationComputeShader::FParameters
SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ReferenceOrbitTexture)
SHADER_PARAMETER_SAMPLER(SamplerState, OrbitSampler)
SHADER_PARAMETER(int32, OrbitLength)
```
- Format: `PF_A32B32G32R32F` (128-bit per texel)
- Layout per iteration `n`:
  - `R,G,B`: `z_ref` (x, y, z) — reference orbit position
  - `A`: spare (or pack derivative component)
- Second texture for derivatives:
  - `R,G,B`: `dzdc` (x, y, z)
- Dimensions: `(MaxIterations, 1)` — 1D texture, point sampling
- Update: CPU recomputes → `UpdateTextureRegions` or render resource update

**Option B: Structured Buffer**
```cpp
SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ReferenceOrbitBuffer)
SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DerivativeBuffer)
```
- Pros: Direct indexing, no texture overhead
- Cons: Requires RDG buffer setup, less cache-friendly than textures

**Reference Center Parameter:**
```cpp
SHADER_PARAMETER(FVector3f, ReferenceCenter)  // C_ref in fractal space
```

**Implementation Location:**
- Extend `FPerturbationComputeShader::FParameters` in `PerturbationShader.h`
- Bind in `FractalSceneViewExtension::RenderFractal_RenderThread`

---

#### 3. **GPU Perturbation Math** ❌
**Status:** Shader uses direct DE iteration, no perturbation formula

**Required Changes to `PerturbationShader.usf`:**

**Replace `MandelbulbDE` with Perturbation Loop:**
```hlsl
struct PerturbationResult
{
    float3 finalZ;      // z_ref + epsilon
    int iterations;
    bool escaped;
};

PerturbationResult MandelbulbPerturbation(float3 C, float3 C_ref)
{
    float3 delta = C - C_ref;  // Offset from reference point
    float3 epsilon = delta;     // Initial perturbation w_0 = deltaC
    
    int iter;
    for (iter = 0; iter < MaxIterations; iter++)
    {
        // Sample reference orbit at iteration iter
        float4 orbitSample = ReferenceOrbitTexture.Load(int3(iter, 0, 0));
        float3 z_ref = orbitSample.xyz;
        
        float4 derivSample = DerivativeTexture.Load(int3(iter, 0, 0));
        float3 dzdc = derivSample.xyz;
        
        // Reconstruct perturbed orbit: z' = z_ref + dzdc * delta + epsilon
        float3 z_perturbed = z_ref + dzdc * delta + epsilon;
        
        // Check escape
        float r = length(z_perturbed);
        if (r > BailoutRadius)
        {
            PerturbationResult result;
            result.finalZ = z_perturbed;
            result.iterations = iter;
            result.escaped = true;
            return result;
        }
        
        // Update perturbation term for next iteration
        // epsilon_{n+1} = g_p(z_ref + epsilon) - g_p(z_ref) + delta
        // where g_p(z_ref) is implicitly z_{ref,n+1} - C_ref (from orbit)
        
        float3 g_perturbed = SphericalPowerTransform(z_perturbed, FractalPower);
        
        // Reference next step (without adding C_ref, since we track orbit separately)
        float4 orbitNext = ReferenceOrbitTexture.Load(int3(iter + 1, 0, 0));
        float3 z_ref_next_noConst = orbitNext.xyz - C_ref;
        
        epsilon = (g_perturbed - z_ref_next_noConst) + delta;
    }
    
    // Max iterations reached
    PerturbationResult result;
    result.finalZ = z_ref + epsilon;
    result.iterations = iter;
    result.escaped = false;
    return result;
}
```

**Distance Estimation with Perturbation:**
- Track derivative `dr` alongside epsilon iteration
- Use perturbed `z` for radius checks
- Return distance estimate based on final perturbed state

**Integration into Ray March:**
- Replace `MandelbulbDE(pos, ...)` calls with `MandelbulbPerturbation(pos, ReferenceCenter)`
- Adapt shading to use perturbation result structure

---

#### 4. **Data Upload Pipeline** ❌
**Status:** No mechanism to transfer orbit data to GPU

**Required Components:**

**In `FractalControlSubsystem` or View Extension:**
```cpp
// Triggered when orbit recomputes
void UploadOrbitToGPU(const TArray<FVector3d>& OrbitHiPrec,
                       const TArray<FVector3d>& DerivativesHiPrec)
{
    // Downcast to float for GPU
    TArray<FVector4f> OrbitData;
    OrbitData.Reserve(OrbitHiPrec.Num());
    for (const FVector3d& ZRef : OrbitHiPrec)
    {
        OrbitData.Add(FVector4f(ZRef.X, ZRef.Y, ZRef.Z, 0.0f));
    }
    
    // Create/update texture or buffer
    // Option 1: Dynamic texture via UpdateTextureRegions
    // Option 2: RDG buffer upload in render pass
    
    // Thread-safe handoff to render thread
    ENQUEUE_RENDER_COMMAND(UploadFractalOrbit)(
        [OrbitData](FRHICommandListImmediate& RHICmdList)
        {
            // Create RDG texture/buffer and upload
        });
}
```

**Render Thread Integration:**
- In `FractalSceneViewExtension::RenderFractal_RenderThread`:
  - Check if orbit texture/buffer is valid
  - Bind to shader parameters
  - Pass reference center as `FVector3f`

---

#### 5. **Precision & Error Management** ❌
**Status:** No error detection or fallback

**Required Features:**

**Glitch Detection:**
- Monitor when `|epsilon|` grows unexpectedly large
- Compare `|z_ref + epsilon|` against expected bounds
- Flag pixels that diverge from perturbation validity

**Fallback Strategy:**
```hlsl
if (length(epsilon) > GLITCH_THRESHOLD * length(z_ref))
{
    // Perturbation broke down, mark for CPU recompute
    // or switch to local reference orbit
    return GLITCH_COLOR; // e.g., magenta
}
```

**Adaptive Reference Selection:**
- For interior points or glitched regions, spawn local reference orbits
- Requires multiple orbit textures or dynamic recompute queue
- Advanced: cluster pixels by reference point

---

## Implementation Roadmap

### Phase 1: CPU High-Precision Infrastructure
**Goal:** Generate reference orbits in extended precision

**Tasks:**
1. Choose precision library:
   - Quick: `long double` (Windows MSVC supports 64-bit, limited)
   - Robust: Boost.Multiprecision (header-only, good UE integration)
   - Advanced: GMP wrapper (licensing considerations)

2. Create `FMandelbulbOrbitGenerator`:
   - Input: `FVector3d C_ref`, power, max iterations, bailout
   - Output: `TArray<FVector3d> Orbit`, `TArray<FVector3d> Derivatives`
   - Implement 3D Mandelbulb iteration in extended precision
   - Compute Jacobian for derivative tracking

3. Integrate into `UFractalControlSubsystem`:
   - Add `TSharedPtr<FMandelbulbOrbitGenerator> OrbitGenerator`
   - Trigger recompute on parameter changes
   - Run on async task to avoid game thread stalls

**Deliverable:** CPU can compute high-precision orbits and log them

---

### Phase 2: GPU Data Structures
**Goal:** Allocate and bind orbit buffers to shader

**Tasks:**
1. Extend `FPerturbationComputeShader::FParameters`:
   - Add texture/buffer parameters for orbit and derivatives
   - Add `ReferenceCenter` vector parameter

2. Create persistent orbit storage:
   - Option: `UTexture2D` asset created at runtime
   - Option: RDG-managed buffer per frame
   - Prefer texture for simplicity (easier sampling)

3. Implement upload in view extension:
   - Convert `TArray<FVector3d>` → `TArray<FVector4f>`
   - Use `GraphBuilder.CreateTexture` + `AddCopyTexturePass`
   - Or structured buffer via `CreateStructuredBuffer` + `AddUploadPass`

4. Bind in `RenderFractal_RenderThread`:
   - Pass orbit texture handle
   - Pass reference center as float3

**Deliverable:** Shader can sample orbit data (validate with debug output)

---

### Phase 3: Perturbation Shader Math
**Goal:** Replace direct iteration with perturbation formula

**Tasks:**
1. Rewrite `MandelbulbDE` as `MandelbulbPerturbation`:
   - Implement epsilon update loop
   - Sample orbit texture per iteration
   - Reconstruct perturbed z

2. Handle derivatives for DE:
   - Track `dr` perturbation term
   - Use reference derivative or recompute on GPU

3. Update ray marcher to use perturbation:
   - Pass `ReferenceCenter` to march function
   - Replace all DE calls

4. Test with simple scenes:
   - Verify iteration counts match CPU reference
   - Check colors against non-perturbation baseline

**Deliverable:** Fractal renders using perturbation (may have glitches)

---

### Phase 4: Orbit Refresh & Error Handling
**Goal:** Automatic recompute and glitch mitigation

**Tasks:**
1. Implement refresh heuristics:
   - Track camera movement vs. reference center
   - Detect zoom changes
   - Trigger recompute + upload when threshold exceeded

2. Add glitch detection:
   - Shader flags pixels with large epsilon
   - CPU-side fallback for problem regions

3. Optimize upload:
   - Reuse texture/buffer across frames
   - Only recompute when necessary
   - Profile upload cost

**Deliverable:** Stable deep-zoom navigation

---

### Phase 5: Optimization & Advanced Features
**Goal:** Performance tuning and series approximation

**Tasks:**
1. Series approximation (optional):
   - Compute Taylor series coefficients on CPU
   - Evaluate polynomial on GPU to skip iterations
   - Requires higher-order derivative tensors

2. Multi-reference support:
   - Divide viewport into tiles
   - Use local reference per tile for interior points

3. Precision profiling:
   - Log effective precision vs. zoom depth
   - Validate against arbitrary-precision CPU render

**Deliverable:** Production-quality perturbation renderer

---

## File-by-File Changes Summary

### New Files to Create
1. **`FractalOrbitGenerator.h/cpp`**
   - High-precision orbit computation
   - Extended-precision complex/vector math
   - Derivative/Jacobian tracking

2. **`PerturbationDataUploader.h/cpp`** (optional)
   - Encapsulates texture/buffer upload
   - Manages orbit asset lifecycle

### Files to Modify

**`FractalParameter.h`**
- Add: `FVector3d ReferenceCenter`
- Add: `int32 OrbitLength`
- Add: `bool bOrbitValid`

**`PerturbationShader.h`**
- Add orbit texture/buffer parameters to `FParameters`
- Add `ReferenceCenter` parameter

**`PerturbationShader.usf`**
- Replace `MandelbulbDE` with `MandelbulbPerturbation`
- Add orbit texture sampling
- Implement epsilon update logic
- Add glitch detection markers

**`FractalSceneViewExtension.h/cpp`**
- Store orbit texture/buffer handle
- Bind orbit data in `RenderFractal_RenderThread`
- Handle orbit upload from game thread

**`FractalControlSubsystem.h/cpp`**
- Add `FMandelbulbOrbitGenerator` member
- Implement orbit recompute on parameter change
- Trigger async orbit generation
- Queue GPU upload

---

## Estimated Complexity

| Component | Complexity | Effort | Risk |
|-----------|-----------|--------|------|
| High-precision math | Medium | 2-3 days | Low (libraries available) |
| Orbit generator | Medium | 3-5 days | Medium (3D derivatives complex) |
| GPU data upload | Low | 1-2 days | Low (standard UE pattern) |
| Shader rewrite | Medium | 3-4 days | Medium (perturbation algebra) |
| Integration & testing | High | 5-7 days | High (numerical stability) |
| Optimization | Medium | 3-5 days | Medium (profiling dependent) |
| **Total** | **Medium-High** | **17-26 days** | **Medium** |

---

## Key Technical Decisions

### 1. Precision Library Choice
**Recommendation:** Start with `long double`, migrate to Boost.Multiprecision if insufficient

**Rationale:**
- `long double` requires no dependencies, easy to test
- Boost.Multiprecision header-only, UE-friendly
- GMP harder to integrate (binary dependency, licensing)

### 2. Orbit Storage Format
**Recommendation:** Texture2D with `PF_A32B32G32R32F`

**Rationale:**
- GPU texture cache optimized for 1D linear reads
- Easier to debug (can visualize in RenderDoc)
- Point sampling avoids interpolation issues
- Simpler code than structured buffers

### 3. Derivative Computation
**Recommendation:** CPU computes full Jacobian, GPU uses linear approximation

**Rationale:**
- Full Jacobian expensive but only done once on CPU
- GPU can reuse or approximate from stored derivatives
- Series approximation (Phase 5) requires this anyway

### 4. Refresh Strategy
**Recommendation:** Threshold-based recompute with hysteresis

**Rationale:**
- Simple to implement
- Prevents thrashing on minor camera wobble
- Can be tuned per-project

---

## Validation Plan

### Unit Tests
1. **Orbit Generator Accuracy:**
   - Compare against known Mandelbulb reference images
   - Validate derivative values numerically

2. **Precision Retention:**
   - Measure effective precision at varying zoom depths
   - Log when perturbation breaks down

### Integration Tests
1. **Visual Comparison:**
   - Render same scene with/without perturbation
   - Diff images at multiple zoom levels

2. **Performance Benchmarks:**
   - Frame time with orbit upload
   - GPU occupancy during perturbation
   - Memory usage (orbit buffers)

### Stress Tests
1. **Deep Zoom:**
   - Navigate to extreme zoom depths (10^-10 scale)
   - Verify stability and glitch handling

2. **Rapid Movement:**
   - Pan/zoom quickly, ensure orbit refreshes
   - Check for visual artifacts during recompute

---

## Conclusion

The current implementation is a **complete and functional standard Mandelbulb ray marcher** but lacks **all perturbation-specific components**. To achieve true perturbation rendering:

**Must Add:**
- ✅ CPU high-precision orbit generation (core enabler)
- ✅ GPU orbit data structures (storage)
- ✅ Perturbation math in shader (algorithm)
- ✅ Upload pipeline (CPU→GPU bridge)
- ✅ Orbit refresh logic (automation)

**Can Defer:**
- Series approximation (optimization)
- Multi-reference support (advanced)
- Glitch auto-correction (quality-of-life)

**Estimated Timeline:** 3-5 weeks for full perturbation pipeline, 1-2 weeks for MVP (basic perturbation without optimizations).

The architecture is **well-suited** for this addition—the scene view extension provides clean injection point, parameter system is extensible, and shader is modular. The primary work is **new code**, not refactoring, which reduces risk.
