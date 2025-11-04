#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "FractalParameter.h"

/**
 * Scene View Extension for rendering fractals directly into the post-process pipeline
 * Automatically renders every frame without needing Blueprint calls
 */
class FRACTALRENDERER_API FFractalSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FFractalSceneViewExtension(const FAutoRegister& AutoRegister);
	virtual ~FFractalSceneViewExtension() = default;

	// FSceneViewExtensionBase interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	
	// Post-process pass subscription
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	// Set fractal parameters from game thread
	void SetFractalParameters(const FFractalParameter& InParams);

private:
	// Callback for rendering the fractal
	FScreenPassTexture RenderFractal_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	// Thread-safe storage for fractal parameters
	FFractalParameter FractalParameters;
	FCriticalSection ParameterMutex;
};
