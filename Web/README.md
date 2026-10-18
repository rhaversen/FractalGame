# Mandelbulb Deep Zoom (browser prototype)

The Mandelbulb with perturbation rendering in WebGPU: one reference orbit in double-double arithmetic on the CPU, one
compute shader that follows every pixel as a float offset from it. It is a port of the desktop renderer in
`Plugins/FractalRenderer` for integer powers 2 to 16 and a single fractal; the maths is the same, see
`PERTURBATION_IMPLEMENTATION_NOTES.md`.

| File | What it is |
| --- | --- |
| `src/lib.js` | CPU side: double-double arithmetic, the Mandelbulb in double-double, reference orbit and its GPU layout, reference-ray fan, iteration budget, teleport to a surface. Runs on the main thread and in a Web Worker. |
| `src/shader.wgsl` | Compute shader: perturbed power map (`perturbedPowerDelta`, port of `FractalPowerMap.ush`), perturbed and plain distance estimate, rebasing, bounding-sphere clip, march, shading. |
| `src/present.wgsl` | Draws the compute texture on the canvas. |
| `src/page.html`, `src/app.js` | Page, HUD, input, WebGPU setup, reference upkeep. |
| `build.py` | Assembles `dist/mandelbulb.html`, one self-contained page. |
| `test/` | `lib.test.js` checks the double-double orbit against mpmath (`gen_cases.py`) and the orbit layout. |

```bash
python3 Web/build.py
python3 Web/test/gen_cases.py /tmp/cases.json && node Web/test/lib.test.js /tmp/cases.json
python3 -m http.server -d Web/dist 8000   # then open http://localhost:8000/mandelbulb.html in a WebGPU browser
```

Differences from the desktop renderer: no linear series skip (the orbit work per pixel is higher at depth, which a GPU
absorbs), integer powers only (no trigonometry in the shader), the distance to the surface comes from a one-thread probe
on the GPU instead of the CPU, and the orbit length is sized from how many iterations the camera's own orbit needs
(orbits next to the surface expand by only ~1.4x per iteration, so 1e-24 needs ~800).
