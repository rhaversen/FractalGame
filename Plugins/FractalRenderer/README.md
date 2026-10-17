# FractalRenderer Plugin

Real-time ray marcher for eight 3D fractals (Mandelbulb, Burning Ship, Julia Set, Mandelbox, Inverted Menger,
Quaternion Julia, Sierpinski Tetrahedron, Kaleidoscopic IFS), integrated into Unreal Engine 5.6 through a custom
Scene View Extension. CPU/GPU perturbation gives zoom far beyond 32-bit float precision (validated down to a
scale of ~1e-27). See `../../PERTURBATION_IMPLEMENTATION_NOTES.md` for the maths and the verification results.

## Folder Layout

- `Source/FractalRenderer/Public/FractalMath/` – engine-free maths shared with `Tools/PerturbationLab`:
  double-double arithmetic, the formulas (`Formulas/*.h`), generic reference orbit + series skip + distance
  estimator + reference-ray march (`FractalReferenceOrbit.h`), camera ray basis.
- `Source/FractalRenderer` – module bootstrap, view extension, reference manager, runtime controls.
- `Shaders/` – pure HLSL: `FractalMathCommon.ush` (elementary functions), `FractalPowerMap.ush`, `FractalFolds.ush`,
  `FractalFoldTracker.ush`, `FractalQuaternion.ush` (exact steps), `FractalDE.ush` (generic perturbed distance
  estimator), `FractalFormula.ush` + `Formula<Name>.ush` (one per fractal, selected by `FP_FRACTAL_TYPE`),
  `FractalRender.ush` (ray march and shading).
- `Shaders/PerturbationShader.usf` – Unreal entry point (parameter binding only).

## Runtime Flow

- `FFractalRendererModule` (runtime, `PostConfigInit`) maps `/FractalRendererShaders`, runs the
  double-double self test and registers `FFractalSceneViewExtension` once the engine is ready.
- `FFractalSceneViewExtension::SubscribeToPostProcessingPass` injects a compute pass after tonemapping (player
  view only, not captures). Each frame it:
  1. maps the view origin to fractal space in double-double (`FFractalCameraMapping`);
  2. builds the per-pixel ray basis from the unjittered projection and the view rotation (double);
  3. asks `FFractalReferenceManager` for a perturbation reference of the selected fractal. The manager marches
     the centre ray on the CPU, places `C_ref` and computes its orbit in double-double, on a worker thread unless
     no usable reference exists;
  4. uploads the orbit (`StructuredBuffer<float4>`) plus `CameraOffset = (camera - C_ref) / Scale` and
     dispatches `FPerturbationComputeShader` with the permutation for the fractal (and the static power-8
     Mandelbulb).
- `UFractalControlSubsystem` (GameInstance subsystem) owns `FFractalParameter` and the world↔fractal mapping and
  pushes both to the view extension.

## Controlling the Fractal

Access the subsystem from Blueprint or C++ via `GetSubsystem<UFractalControlSubsystem>()`.

- Fractal: `SetFractalType(EFractalType)`, `SetFractalPower(float)` (exponent of the power fractals, scale factor of
  the folding fractals), `GetPreset(Type)` (interactive ranges and defaults), `GetFractalDisplayName(Type)`.
- Rendering tunables (`MaxRaySteps`, `MaxRayDistance`, `MaxIterations`, `BailoutRadius`, `DirectEvaluationFootprint`):
  `GetFractalParameters()` / `SetFractalParameters(FFractalParameter)`; `SetEnabled(bool)` toggles the effect.
- Camera mapping (`Fractal = Origin + (World - Anchor) * Scale`, `Origin` in double-double):
  - `SetUserScale` / `GetUserScale`: the fractal's scale before any camera zoom (unzoomed it scales the camera's fractal
    position about the fractal origin; zoomed it only changes units);
  - `ZoomAroundCamera(Factor)`: rescales world units around the camera without changing the view (Factor < 1 zooms
    in; clamped to `[MinFractalScale, UserScale]`; returns the factor applied); `GetZoomFactor()`, `GetFractalScale()`;
  - `ResetView(FractalOriginWorld)`: unzoomed mapping with the fractal origin at a world position.
- Queries: `GetDistanceAtWorld(WorldPosition)` (CPU distance estimate of the exact formula and precision the GPU
  draws), `GetCameraFractalPosition()`, `GetCameraFractalPositionDD()`, `FormatCoordinate(value, decimals)`.
- Example (C++ `BeginPlay`):

  ```cpp
  if (UFractalControlSubsystem* Fractal = GetGameInstance()->GetSubsystem<UFractalControlSubsystem>())
  {
      Fractal->SetFractalType(EFractalType::Mandelbox);
      Fractal->SetFractalPower(UFractalControlSubsystem::GetPreset(EFractalType::Mandelbox).DefaultPower);
      Fractal->ZoomAroundCamera(1.0e-3); // 1000x deeper around the current camera position
  }
  ```
