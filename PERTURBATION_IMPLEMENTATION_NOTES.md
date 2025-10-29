# Mandelbrot Perturbation Implementation Notes

These notes capture the core ideas for building a robust high-precision Mandelbrot (or Julia) renderer in Unreal Engine where the CPU supplies accurate reference data and the GPU performs the bulk of the pixel work. The goal is to combine the convenience of the engine's rendering pipeline with the numerical stability needed for deep zooms, while avoiding low-precision float math on the GPU.

## 1. High-Level Pipeline

- **CPU generates a reference orbit** at very high precision (long double or arbitrary-precision library). This orbit represents the sequence of complex values `z_n` for a reference point `c_ref` near the visible region.
- **CPU also computes first-order derivatives** (`dz/dc`) alongside the orbit. With both `z_n` and `dz/dc`, the GPU can reconstruct nearby orbits using perturbation theory with simple float operations.
- **GPU receives the orbit + derivatives** in a 1D texture (or structured buffer). Each texel stores `z_ref` and `dzdc` as float2 pairs, providing enough data to perturb any pixel relative to `c_ref`.
- **GPU shader evaluates perturbations per pixel**, accumulating the delta from the reference orbit until escape or iteration limit, while running inside a post-process material for full-screen rendering.
- **Camera/viewport updates trigger CPU recompute**: whenever the user pans, zooms, or rotates sufficiently, the CPU picks a new `c_ref`, rebuilds the orbit, uploads it, and the GPU shader reacts immediately via updated parameters.

## 2. CPU Responsibilities

- **Arbitrary precision complex type.** Use `long double` on platforms that provide 80-bit precision or integrate a multiprecision library (Boost.Multiprecision, GMP, etc.). Implement basic operations (add, multiply, magnitude squared) tailored for the perturbation workflow.
- **Orbit generation.**
  - Choose `c_ref` (typically the viewport center) expressed in high precision.
  - Iterate Mandelbrot recurrence `z_{n+1} = z_n^2 + c_ref` up to `MaxIterations`.
  - Store each `z_n` and the derivative `dz_{n+1}/dc = 2 z_n * dz_n/dc + 1`, with `dz_0/dc = 0`.
  - Optionally keep an `escapeTime` for early bailout if the reference point leaves the set, but for interior points we still keep the full orbit.
- **Texture packing.** Organize orbit data into a contiguous float array:
  - Texel RG stores `z_n` real/imag.
  - Texel BA stores derivative real/imag (or pack across multiple texels for extended precision).
  - Use a `Texture2D` with height 1, format `PF_A32B32G32R32F` for maximal fidelity.
- **Parameter updates.** Maintain a struct reflecting the current viewport (`center`, `scale`, `maxIterations`, `referenceCenter`, etc.) and push to the GPU as vector/scalar parameters on the material instance.
- **Orbit refresh policy.** Recompute when:
  - The delta between new `c_ref` and previous exceeds a threshold relative to `scale`.
  - Zoom factor changes beyond a tolerance (e.g., scale halved/doubled).
  - User requests a precision refresh.

## 3. GPU Responsibilities

- **Perturbation shader.**
  - Inputs: screen UV, viewport params (`center`, `scale`), reference center, maximum iterations, orbit texture.
  - For each iteration `n`, sample `z_ref` and `dzdc` from the texture, compute `deltaZ = z_ref + dzdc * deltaC`, where `deltaC = c_pixel - c_ref`.
  - Accumulate `deltaZ` by iterating the perturbation recurrence: `w_{n+1} = 2 * z_ref * w_n + w_n^2 + deltaC`, starting with `w_0 = 0`. A simplified approach multiplies derivative terms directly; both require precise algebra implemented in float.
  - Test `|deltaZ|^2` against escape radius (commonly 4). Once escaped, compute smooth iteration count (`n + 1 - log2(log|deltaZ|)`) for coloring.
  - Output an emissive color based on normalized escape time and optional additional data (binary interior/exterior, distance estimation, etc.).
- **Texture sampling strategy.** Use point sampling (`SamplerState` with point filter) to avoid interpolation between orbit samples. Fetch with `Texture.Load(int3(iteration, 0, 0))` to maintain deterministic data.
- **Parameter uniformity.** Provide viewport data as float4 parameters; the GPU can stay entirely in float space if deltas remain small enough thanks to the high-precision CPU orbit.

## 4. Data Layout & Precision Considerations

- **Orbit length vs. texture size.** If `MaxIterations` is large, ensure texture dimension matches exactly; Unreal supports up to `2^15` width for TEXTURE2D in practice, but confirm with feature level.
- **Dynamic updates.** Reinitialize the texture each time MaxIterations changes or when reference orbit recomputed; reuse the same UTexture2D asset with `UpdateTextureRegions` or use a render resource update on the render thread.
- **Precision budgeting.**
  - Keep `deltaC` small by recentering whenever the camera drifts; this ensures `float` math on GPU remains stable.
  - Optionally split `deltaC` into high/low parts to extend effective precision (TwoSum or Dekker technique) if necessary.
  - Track accumulated error; if `|deltaZ|` diverges significantly from reference, trigger a CPU refresh.

## 5. Integration in Unreal Engine

- **Subsystem / manager.** Encapsulate CPU work in a `UGameInstanceSubsystem` or similar service. It handles viewport changes, orbit computation, and texture uploads.
- **Dynamic material instance.** Store a `UMaterialInstanceDynamic` (MID) bound to a post-process volume or mesh. Update parameters directly on the MID whenever the subsystem recomputes values.
- **Post-process actor.** Spawn a global unbound post-process volume actor that applies the material to the entire viewport. Alternatively, render to a plane or user widget if post-processing isn't desirable.
- **Threading.** Orbit generation can be moved to a background thread (FRunnable or AsyncTask) to avoid hitches. Ensure results are marshaled back to the game thread before touching UObjects.
- **Editor vs. runtime.** Material assets should be authored manually in the editor. At runtime, simply load the asset, create a MID, and push parameter updates.

## 6. Orbit & Derivative Packaging Strategy

- **Float packing.** Standard layout example (per iteration):
  - `R`: `z_ref.x`
  - `G`: `z_ref.y`
  - `B`: `dzdc.x`
  - `A`: `dzdc.y`
  - For higher precision, use two texels per iteration (hi/lo components) or use encoded mantissa/exponent pairs.
- **Texture creation.** Use a `UTexture2D` with `PF_A32B32G32R32F` format, size `(MaxIterations, 1)`. Initialize using `FTexture2DMipMap::BulkData` when first created, then reuse via updates.
- **Sampler bind.** In the material, sample via `Custom` HLSL or `TextureSampleLevel` node with explicit integer coordinates to disable filtering.

## 7. Perturbation Shader Algorithm Outline

```hlsl
float3 MandelbrotPerturbation(
    float4 ScreenPos,
    float4 Viewport,
    float4 Reference,
    float MaxIter,
    Texture2D<float4> OrbitTex)
{
    float2 uv = ScreenPos.xy / ScreenPos.w;
    float2 c = Viewport.xy + (uv - 0.5f) * Viewport.z;
    float2 deltaC = c - Reference.xy;

    float2 w = float2(0, 0);
    float escapeIter = MaxIter;
    float2 zLast = float2(0, 0);

    [loop]
    for (int iter = 0; iter < MaxIter; ++iter)
    {
        float4 orbit = OrbitTex.Load(int3(iter, 0, 0));
        float2 zRef = orbit.xy;
        float2 dzdc = orbit.zw;

        float2 zPerturbed = zRef + dzdc * deltaC + w;
        zLast = zPerturbed;

        if (dot(zPerturbed, zPerturbed) > 4.0f)
        {
            escapeIter = iter;
            break;
        }

        // Update perturbation term for next iteration
        w = float2(
            2.0f * (zRef.x * w.x - zRef.y * w.y) + (w.x * w.x - w.y * w.y),
            2.0f * (zRef.x * w.y + zRef.y * w.x) + (2.0f * w.x * w.y)
        ) + deltaC;
    }

    float shade = escapeIter / MaxIter;
    return float3(shade, shade * shade, shade * 0.5f);
}
```

Fine-tune the combinational logic (especially the perturbation update) to match the exact perturbation formulation adopted on the CPU side.

## 8. Error Management & Refresh Logic

- **Error detection.** Track the magnitude of the perturbation term or the difference between expected and actual magnitudes; large growth indicates the perturbation has drifted.
- **Graceful fallback.** If the orbit texture becomes invalid (e.g., missing asset, zero entries), render a diagnostic color (magenta) and queue a CPU recompute.
- **Adaptive iterations.** Increase `MaxIterations` based on zoom level. Use CPU heuristics to prevent expensive recomputes if the viewport moves only slightly.

## 9. Optimization Opportunities

- **Derivative skipping.** For interior points, store fewer derivative samples (e.g., every other iteration) and reconstruct via interpolation if needed.
- **Orbit caching.** Keep a pool of previously computed orbits keyed by `c_ref`/scale to reuse while panning.
- **GPU branching.** Break the loop early with `[loop]` + `break` to reduce iteration cost.
- **Material permutations.** Author multiple materials for different quality levels (single precision only, double-sampled, distance estimation, etc.).

## 10. Testing & Validation

- **Compare against CPU render.** Periodically render a patch purely on the CPU and diff against GPU output to validate perturbation accuracy.
- **Stress zoom levels.** Incrementally zoom to extreme depths while monitoring numeric stability and performance.
- **Profiling.** Use Unreal's profiling tools (stat GPU, stat unit) to ensure orbit upload and shader cost stay reasonable.

---

These notes should serve as a reusable blueprint for building a perturbation-assisted renderer in Unreal Engine. The exact asset names and class structures can be simplified or expanded depending on how lean the next implementation needs to be, but the underlying CPU/GPU collaboration model remains the same.
