# FractalRenderer Plugin

Real-time Mandelbulb ray marcher integrated into Unreal Engine 5.6 through a custom Scene View Extension,
with CPU/GPU perturbation for zoom far beyond 32-bit float precision (validated down to a scale of
~1e-27). See `../../PERTURBATION_IMPLEMENTATION_NOTES.md` for the maths and the verification results.

## Folder Layout

- `Source/FractalRenderer/Public/FractalMath/` – engine-free maths shared with `Tools/PerturbationLab`:
  double-double arithmetic, reference orbit + series skip, reference-ray march, camera ray basis.
- `Source/FractalRenderer` – module bootstrap, view extension, reference manager, runtime controls.
- `Shaders/FractalPerturbation.ush` – GPU perturbation maths (pure HLSL).
- `Shaders/FractalRender.ush` – per-pixel ray march and shading (pure HLSL).
- `Shaders/PerturbationShader.usf` – Unreal entry point (parameter binding only).

## Runtime Flow

- `FFractalRendererModule` (runtime, `PostConfigInit`) maps `/FractalRendererShaders`, runs the
  double-double self test and registers `FFractalSceneViewExtension` once the engine is ready.
- `FFractalSceneViewExtension::SubscribeToPostProcessingPass` injects a compute pass after tonemapping.
  Each frame it:
  1. maps the view origin to fractal space in double-double (`FFractalCameraMapping`);
  2. builds the per-pixel ray basis from the unjittered projection and the view rotation (double);
  3. asks `FFractalReferenceManager` for a perturbation reference. The manager marches the centre ray
     on the CPU, places `C_ref` and computes its orbit in double-double, on a worker thread unless no
     usable reference exists;
  4. uploads the orbit (`StructuredBuffer<float4>`) plus `CameraOffset = (camera - C_ref) / Scale` and
     dispatches `FPerturbationComputeShader`. The power-8 permutation is used when the power is exactly 8.
- `UFractalControlSubsystem` (GameInstance subsystem) owns `FFractalParameter` and the world↔fractal
  mapping and pushes both to the view extension.

## Controlling the Fractal

- Access the subsystem from Blueprint or C++ via `GetSubsystem<UFractalControlSubsystem>()`.
- Rendering: `SetEnabled`, `SetMaxRaySteps`, `SetMaxRayDistance`, `SetMaxIterations`, `SetBailoutRadius`,
  `SetMinIterations`, `SetConvergenceFactor`, `SetFractalPower`, or `SetFractalParameters`.
- Camera mapping (`Fractal = Origin + (World - Anchor) * Scale`, `Origin` in double-double):
  `SetFractalScale`, `ZoomAroundCamera(Factor)` (Factor < 1 zooms in, the camera does not move),
  `SetCameraFractalPosition`, `GetCameraFractalPosition`, `GetCameraDistanceEstimate`, `ResetCameraMapping`.
  C++ callers can use `GetCameraMapping` / `SetCameraMapping` for full precision.
- The pawn zooms with the mouse wheel (`Zoom` axis mapping). The pawn keeps moving in world units, so after
  zooming in it travels proportionally slower through the fractal.
- Example (C++ `BeginPlay`):

  ```cpp
  if (UFractalControlSubsystem* Fractal = GetGameInstance()->GetSubsystem<UFractalControlSubsystem>())
  {
      Fractal->SetMaxIterations(256);
      Fractal->ZoomAroundCamera(1.0e-3); // 1000x deeper around the current camera position
  }
  ```

- `SetEnabled(false)` disables the effect when transitioning or debugging post-process issues.
- Removed with this version: the 2D `SetCenter` / float `SetZoom` (replaced by the mapping above),
  `FMandelbulbOrbitGenerator` (replaced by `FractalMath::GenerateReferenceOrbit`) and the unused
  Blueprint async node `ExecutePerturbationShader`.
