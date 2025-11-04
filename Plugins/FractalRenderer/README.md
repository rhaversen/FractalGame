# FractalRenderer Plugin

Real-time Mandelbulb ray marcher integrated into Unreal Engine 5.6 through a custom Scene View Extension.

## Folder Layout

- `Source/FractalRenderer` – module bootstrap, view extension, runtime controls.
- `Shaders/PerturbationShader.usf` – compute shader that performs distance-estimation ray marching.
- `Resources/` – plugin icons and descriptors.
- `Binaries/`, `Intermediate/`, `Saved/` – generated artifacts; do not edit by hand.

## Runtime Flow

- `FFractalRendererModule` (runtime, `PostConfigInit`) maps `/FractalRendererShaders` and registers `FFractalSceneViewExtension` once the engine is ready.
- `FFractalSceneViewExtension::SubscribeToPostProcessingPass` injects a compute pass right after tonemapping. It ray marches a Mandelbulb using camera matrices, mixes the result with the scene color, and writes the output back to the post-process graph.
- `UFractalControlSubsystem` (GameInstance subsystem) stores `FFractalParameter` and pushes updates to the view extension.
- `FPerturbationComputeShader` consumes the full parameter block (camera matrices, ray-march limits, bailout, convergence) and produces a float RGBA render target each frame.

## Controlling the Fractal

- Access the subsystem from Blueprint or C++ via `GetSubsystem<UFractalControlSubsystem>()`.
- Setters expose all tunables: `SetEnabled`, `SetCenter`, `SetZoom`, `SetMaxRaySteps`, `SetMaxRayDistance`, `SetMaxIterations`, `SetBailoutRadius`, `SetMinIterations`, `SetConvergenceFactor`, `SetFractalPower`.
- Example (C++ `BeginPlay`):

  ```cpp
  if (UFractalControlSubsystem* Fractal = GetGameInstance()->GetSubsystem<UFractalControlSubsystem>())
  {
      Fractal->SetZoom(2.5f);
      Fractal->SetCenter({0.4f, 0.2f});
      Fractal->SetMaxIterations(256);
  }
  ```

- Disable the effect with `SetEnabled(false)` when transitioning or debugging post-process issues.
