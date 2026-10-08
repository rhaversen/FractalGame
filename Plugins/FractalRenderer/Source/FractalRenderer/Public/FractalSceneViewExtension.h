#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "FractalParameter.h"
#include "FractalReference.h"
#include "HAL/CriticalSection.h"

/** Per-frame numbers the HUD / gameplay may want to show (written on the render thread). */
struct FRACTALRENDERER_API FFractalRenderStats
{
	double CameraDistanceEstimate = 0.0; // world units, from the latest reference march
	double ReferenceDistance = 0.0;      // world units from the camera to C_ref
	int32 OrbitLength = 0;
	bool bReferenceHit = false;
	double LastGenerationMilliseconds = 0.0;
};

/**
 * Scene View Extension that ray marches the fractal into the post-process chain (after tonemapping).
 * Every frame it maps the view origin into fractal space in double-double precision, makes sure a
 * nearby perturbation reference exists (FFractalReferenceManager) and dispatches the compute shader.
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
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	/** Game thread: rendering tunables. */
	void SetFractalParameters(const FFractalParameter& InParams);

	/** Game thread: world <-> fractal mapping (position and scale). */
	void SetCameraMapping(const FFractalCameraMapping& InMapping);

	/** Any thread. */
	FFractalRenderStats GetRenderStats() const;

private:
	FScreenPassTexture RenderFractal_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	FFractalParameter FractalParameters;
	FFractalCameraMapping CameraMapping;
	FFractalRenderStats RenderStats;
	mutable FCriticalSection StateMutex;

	FFractalReferenceManager ReferenceManager;
};
