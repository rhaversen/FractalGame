# PerturbationLab

Standalone validation of the FractalRenderer perturbation maths, outside Unreal. It compiles the
**same files the plugin uses**:

* `Plugins/FractalRenderer/Source/FractalRenderer/Public/FractalMath/*.h` (C++, engine-free)
* `Plugins/FractalRenderer/Shaders/FractalPerturbation.ush`, `FractalRender.ush` (HLSL, compiled with DXC
  exactly like Unreal's SM6/Vulkan path: `-Zpr -O3 -HV 2021`), executed on Vulkan.

Ground truth is `__float128` (libquadmath) and, where even 113 bits are not enough, mpmath.
See `../../PERTURBATION_IMPLEMENTATION_NOTES.md` for the maths and the results.

## Requirements

* g++ (or clang) with libquadmath, `make`
* Vulkan loader + any Vulkan 1.1 device. Mesa lavapipe (CPU) works: `apt install mesa-vulkan-drivers libvulkan-dev`
* DXC (DirectXShaderCompiler) Linux release, default path `/opt/dxc` (`make DXC=... DXC_LIB=...` to override)
* Python 3 with `mpmath` and `Pillow` for `verify_step_mpmath.py`, `polar_chaos.py`, `make_panel.py`

## Running

```
make test            # everything below that has a pass/fail verdict
make ue-shader-check # compile PerturbationShader.usf (engine include stripped), both permutations, DXIL + SPIR-V
make images          # renders out/images/*.ppm and out/comparison.png
```

| Program | What it checks |
|---|---|
| `out/test_dd` | double-double arithmetic, sqrt, exp, log, sin/cos, atan2, pow vs `__float128`; runtime self test |
| `out/test_gpu_math` | GPU elementary functions (`FP_SinCos`, `FP_Atan2`, `FP_Log1p`, `FP_Expm1`) and one perturbation step `g(Z+d)-g(Z)` for powers 2, 3, 7.5, 8: generic, on/near the polar axis, near the origin, large offsets, `|d|/|Z|` from 1e-35 to 1e2 |
| `scripts/verify_step_mpmath.py` | the step for `|d|/|Z| < 1e-25` against 80-digit mpmath (quad cannot resolve those differences) |
| `out/test_perturbed_de [samples]` | full perturbed distance estimator vs quad at 14 zoom depths (1e-3 … 1e-29), 5 surface locations, reference inside / outside the surface; also plain float and a double-double conditioning probe |
| `out/test_camera` | Unreal matrix conventions (`mul(v, M)` with `-Zpr`), ray basis vs FOV / orientation / per-pixel unprojection / the previous shader's `GetCameraRay`, float upload error |
| `out/render_lab [w h truth scene]` | complete renders at 1e-1 … 1e-27 through the production CPU path (`MarchReferenceRay`, `GenerateReferenceOrbit`, `BuildRayBasis`), GPU perturbation vs plain float vs a CPU double-double ground truth running the same march |
| `out/bench` | per-iteration ALU cost of the perturbed step, the plain trig-free step and the previous shader's step |
| `scripts/polar_chaos.py` | shows that the +z pole bottleneck is numerically chaotic (escape iteration changes between 40/60/100 digits) |

Environment knobs: `LAB_DIRECT_FOOTPRINT` (hybrid threshold, 0 = always perturb), `LAB_SERIES_TOL`
(series skip tolerance), `LAB_CONVERGENCE` (old convergence early-out), `LAB_STEPS=1` (march / iteration
statistics), `LAB_VERBOSE=1`, `LAB_DUMP=1` (worst cases).

Timings come from lavapipe, i.e. a CPU; only ratios between kernels are meaningful.
