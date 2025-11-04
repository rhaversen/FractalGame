#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "FractalParameter.h"
#include "MandelbulbOrbitGenerator.h"

// Forward declarations
class UFractalControlSubsystem;

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

	// Set reference orbit data (called by subsystem when orbit regenerates)
	void SetReferenceOrbit(const FReferenceOrbit& InOrbit);

private:
	// Callback for rendering the fractal
	FScreenPassTexture RenderFractal_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	// Create and upload orbit texture to RDG
	FRDGTextureRef CreateOrbitTexture(FRDGBuilder& GraphBuilder, const TArray<FVector4f>& OrbitData);

	// Thread-safe storage for fractal parameters
	FFractalParameter FractalParameters;
	FCriticalSection ParameterMutex;

	// Thread-safe storage for orbit data
	TArray<FVector4f> OrbitFloatData;
	FVector3d CurrentReferenceCenter;
	int32 CurrentOrbitLength;
	FCriticalSection OrbitMutex;
};
