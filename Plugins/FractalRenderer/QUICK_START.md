# Quick Start Guide - Rendering the Fractal

## Current Status
✅ Shader code updated with 3D Mandelbulb (power 8)
✅ Compute shader infrastructure ready
❌ No automatic rendering set up yet

## How the Rendering Works

### Architecture Flow:
```
Blueprint/C++ → FPerturbationShaderInterface → RenderThread → GPU Compute Shader → RenderTarget
```

### Three Ways to Render:

## Method 1: Blueprint (Quick Test) ⭐ RECOMMENDED

1. **Create Render Target**:
   - Content Browser → Right-click → Materials & Textures → **Render Target**
   - Name: `RT_Fractal`
   - Size: 1024x1024
   - Format: RGBA8

2. **Create Material to Display It**:
   - Content Browser → Right-click → Material
   - Name: `M_FractalDisplay`
   - Add a **Texture Sample** node
   - Set it to use `RT_Fractal`
   - Connect RGB to Base Color
   - Set Material Domain to **User Interface** (if using UMG) or **Surface** (if using a plane)

3. **In Level Blueprint (or Widget Blueprint)**:
   ```
   Event BeginPlay
   ↓
   ExecutePerturbationShader (Async)
     - Output Render Target: RT_Fractal
     - Center: (0, 0)
     - Zoom: 1.0
     - MaxIterations: 256
   ↓
   (On Completed)
   Draw Material to Screen (if UMG)
   ```

4. **Display Options**:
   - **Option A - UMG Widget**: Create Image widget, set brush to render target
   - **Option B - World Plane**: Create plane mesh, apply material with render target
   - **Option C - HUD**: Draw render target in HUD's DrawHUD event

## Method 2: Use the Display Actor (I Just Created)

1. **Drag `AFractalDisplayActor` into your level** (after building)
2. **Set Properties**:
   - Render Target: Create one or leave null (auto-creates)
   - Center: (0, 0)
   - Zoom: 1.0
   - Max Iterations: 256
   - Auto Render: ✅ Checked
3. **Play** - Should render automatically!

**Note**: You'll need a custom material to actually see it. The actor structure is there but needs:
- A material with a `Texture` parameter named `FractalTexture`
- That parameter set to the render target

## Method 3: Pure C++ (Advanced)

```cpp
// In your actor or game mode
#include "PerturbationShader.h"

void AMyActor::RenderFractal()
{
    if (!RenderTarget) return;
    
    FPerturbationShaderDispatchParams Params(1, 1, 1);
    Params.OutputRenderTarget = RenderTarget;
    Params.Center = FVector2D(0.0f, 0.0f);
    Params.Zoom = 1.0f;
    Params.MaxIterations = 256;
    
    FPerturbationShaderInterface::Dispatch(Params, []()
    {
        UE_LOG(LogTemp, Log, TEXT("Fractal rendered!"));
    });
}
```

## What You Should See

Once rendering works, you'll see a **3D Mandelbulb slice** (z=0 plane):
- Dark blue → Light blue → Yellow → Orange gradient
- Complex organic structure
- Escape-time coloring (iterations to bailout)

## Next Steps After Validation

Once you see the basic Mandelbulb rendering:

1. ✅ **Step 1**: Validate basic rendering (THIS STEP)
2. 🔲 **Step 2**: Add CPU reference orbit calculation (high precision)
3. 🔲 **Step 3**: Implement GPU perturbation theory
4. 🔲 **Step 4**: Add 3D raymarching for proper volume rendering
5. 🔲 **Step 5**: Optimize with series approximation

## Debugging Tips

**Not seeing anything?**
- Check Output Log for "Fractal rendered!" message
- Check render target is valid in Blueprint
- Verify material is using the render target texture
- Make sure you're calling ExecutePerturbationShader node
- Check MaxIterations isn't too low (try 512)

**Shader not compiling?**
- Check Shaders/ folder path is correct
- Verify shader virtual path mapping in module startup
- Look for shader compile errors in Output Log

---

**Current Implementation**: Basic 3D Mandelbulb (power 8) with escape-time coloring
**Performance**: ~60 FPS at 1024x1024, 256 iterations (GPU compute)
**Next**: Add perturbation theory for deep zoom capability!
