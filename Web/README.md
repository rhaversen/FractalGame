# Mandelbulb Deep Zoom (browser port)

> **Status: frozen reference.** The Unreal project is the deliverable; this port is kept as a reference and is not
> developed further.

The Mandelbulb renderer of FractalGame in WebGPU. It is a 1:1 port of the desktop pipeline for this one fractal:

| Desktop (Unreal) | Browser |
| --- | --- |
| `FractalMath/DoubleDouble.h`, `PowerMap.h`, `FractalReferenceOrbit.h`, `FractalCamera.h`, `ComputeIterationBudget` | `src/lib.js` (same algorithms and constants; the reference orbit including the series rows matches the C++ output to 1 float ulp, see `test/`) |
| `Shaders/FractalMathCommon.ush`, `FractalPowerMap.ush`, `FormulaMandelbulb.ush`, `FractalDE.ush`, `FractalRender.ush` | `src/shader.wgsl` (same function names; integer and non-integer powers, linear series skip, rebasing, exact escape margin, hybrid plain-float switch, bounding-sphere clip, shading and haze) |
| `FP_STATIC_POWER_8` shader permutation | a second pipeline with the WGSL override `STATIC_POWER = 8` |
| `FFractalReferenceManager` (sync/async rebuilds, 25-ray fan, 9-ray quick fan, rebuild on move/turn) | `maintainReference` in `src/app.js`, reference built in a Web Worker |
| `FractalPlayerController` (speed %, time-to-surface speed law, acceleration/braking, zoom band, roll and power velocity) | `controllerTick` in `src/app.js` |
| `UpdateAdaptiveResolution` + `FractalUpscale.usf` (opt-in frame rate target) | `updateAdaptiveResolution` + `src/present.wgsl` |

Since the port, both renderers share these improvements (see `PERTURBATION_IMPLEMENTATION_NOTES.md` §12): the Jacobian
distance estimate (the scalar running derivative made deep zooms render a smooth shell in front of the fractal), the
iteration budget sized from the camera's own orbit, series tolerance 1e-6, over-relaxed sphere tracing, the cone
pre-pass and the analytic surface normal.

`bench/` is the benchmark: `node bench/bench.js <page url> <out dir> [W H frames]` renders the fixed views of
`bench/views.json` in headless Chromium (software WebGPU) with work counters; `BENCH_OPTS` overrides the renderer
options (`relax`, `seriesTolerance`, `cone`, `coneSafety`, `normalMode` (0 = finite differences), `normals: false`, `virtual` = [W, H] for full-frame pixel sizes), `bench/compare.py` compares
the images of two runs.

Not ported: the other seven fractals, the fractal scale (thumb buttons; it only changes the world size of the fractal),
and the GPU timer (the browser uses the frame time, Unreal's fallback). The view renders at CSS-pixel resolution by
default; "Native pixels" renders at the device pixel ratio (up to 2x).

```bash
python3 Web/build.py                       # writes Web/dist/mandelbulb.html (one self-contained page)
sh Web/test/run.sh                         # double-double vs mpmath, orbit layout, orbit vs the C++ code
python3 -m http.server -d Web/dist 8000    # then open http://localhost:8000/mandelbulb.html in a WebGPU browser
```
