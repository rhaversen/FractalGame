# FractalRenderer Plugin

High-precision perturbation-based fractal renderer for deep-zoom Mandelbrot sets in Unreal Engine 5.6.

## Architecture

This plugin follows Epic's recommended pattern for custom rendering effects:

### Module Structure
- **Type**: Runtime module with `PostConfigInit` loading phase
- **Dependencies**: 
  - Public: Core, CoreUObject, Engine, RenderCore, RHI, Projects
  - Private: Renderer (for FRDGBuilder, FSceneView, etc.)

### Key Components

#### 1. Perturbation Compute Shader (`PerturbationShader.usf`)
- Location: `Plugins/FractalRenderer/Shaders/`
- Virtual Path: `/FractalRendererShaders/PerturbationShader.usf`
- Currently implements basic Mandelbrot iteration
- **TODO**: Implement perturbation theory (CPU reference orbit + GPU delta calculation)

#### 2. FPerturbationComputeShader (C++)
- Global shader class using RDG (Render Dependency Graph)
- Thread configuration: 8x8x1
- Parameters:
  - `Center`: Complex plane center point
  - `Zoom`: Zoom level
  - `MaxIterations`: Maximum iteration count
  - `OutputTexture`: RWTexture2D output

#### 3. FPerturbationShaderInterface
- Static interface for dispatching shader from any thread
- Handles game thread → render thread marshaling via `ENQUEUE_RENDER_COMMAND`
- Creates RDG pass with proper UAV setup

#### 4. UPerturbationShaderLibrary_AsyncExecution
- Blueprint-callable async node
- Exposes shader execution to Blueprint graphs
- Event-driven completion callback

## Usage

### From Blueprint
1. Call `ExecutePerturbationShader` node (async)
2. Provide a `TextureRenderTarget2D` for output
3. Set Center, Zoom, MaxIterations
4. Connect to `Completed` event for callback

### From C++
```cpp
FPerturbationShaderDispatchParams Params(1, 1, 1);
Params.Center = FVector2D(0.0, 0.0);
Params.Zoom = 1.0;
Params.MaxIterations = 256;
Params.OutputRenderTarget = MyRenderTarget;

FPerturbationShaderInterface::Dispatch(Params, []()
{
    // Completion callback
});
```

## Next Steps (Perturbation Implementation)

### Phase 1: CPU Reference Orbit
- [ ] Create `UFractalOrbitSubsystem` (UGameInstanceSubsystem)
- [ ] Implement high-precision complex arithmetic (long double or arbitrary precision)
- [ ] Generate reference orbit: z_ref[n] for n = 0 to MaxIterations
- [ ] Calculate derivatives: dzdc_ref[n]
- [ ] Store in 1D texture (PF_A32B32G32R32F): [z_ref.real, z_ref.imag, dzdc.real, dzdc.imag]

### Phase 2: GPU Perturbation Shader
- [ ] Modify `PerturbationShader.usf` to read orbit texture
- [ ] Implement perturbation iteration:
  ```
  w_{n+1} = 2*z_ref[n]*w_n + w_n^2 + deltaC
  ```
- [ ] Use reference orbit for automatic precision scaling
- [ ] Smooth coloring with derivative-based distance estimation

### Phase 3: Scene View Extension (Optional)
- [ ] Create `FFractalSceneViewExtension : public FSceneViewExtensionBase`
- [ ] Override `PrePostProcessPass_RenderThread` or `SubscribeToPostProcessingPass`
- [ ] Automatic per-frame rendering integration
- [ ] Integration with post-process materials

## Module Architecture Benefits

✅ **No Incomplete Type Errors**: Plugin module has full access to Renderer module types  
✅ **Clean Separation**: Game code remains simple, complex rendering in plugin  
✅ **Shader Virtual Paths**: Properly mapped in module startup  
✅ **Blueprint Friendly**: Async nodes for easy prototyping  
✅ **Performance**: RDG-based rendering with compute shaders  

## Building

1. Regenerate project files:
   ```powershell
   & "C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" -projectfiles
   ```

2. Build the editor:
   ```powershell
   Run task: "FractalGameEditor Win64 Development Build"
   ```

3. Enable plugin in `.uproject` or via Editor Plugin Browser

## References
- Based on ShadeupExamplePlugin compute shader pattern
- Epic's documentation on Scene View Extensions
- Perturbation theory implementation notes in `PERTURBATION_IMPLEMENTATION_NOTES.md`
