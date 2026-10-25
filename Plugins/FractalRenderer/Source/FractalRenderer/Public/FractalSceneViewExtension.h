#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "FractalParameter.h"
#include "FractalReference.h"
#include "HAL/CriticalSection.h"

/** What the renderer did in the last frame it drew the fractal (for telemetry and the flight recorder). */
struct FRACTALRENDERER_API FFractalRenderStats
{
	int32 Iterations = 0;              // iteration limit of the view (held with hysteresis)
	int32 OrbitLength = 0;             // reference orbit points (0 = no usable reference)
	uint64 ReferenceVersion = 0;       // changes whenever the reference is rebuilt
	double ReferenceMilliseconds = 0.0; // CPU time that built the current reference
	bool bReferenceHit = false;        // the reference rays found the surface
	FIntPoint MarchExtent = FIntPoint::ZeroValue; // marched pixels (below the view size under adaptive resolution)
	uint64 FrameNumber = 0;            // render-thread frame counter, 0 before the first frame
};

/**
 * Scene View Extension that ray marches the selected fractal into the post-process chain (after tonemapping).
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

	/** Any thread: statistics of the last rendered frame. */
	FFractalRenderStats GetRenderStats() const;

private:
	FScreenPassTexture RenderFractal_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	FFractalParameter FractalParameters;
	FFractalCameraMapping CameraMapping;
	FCriticalSection StateMutex;

	FFractalReferenceManager ReferenceManager;
	int32 HeldIterations = 0; // iteration limit of the view (render thread), changed with hysteresis

	FFractalRenderStats RenderStats;
	mutable FCriticalSection StatsMutex;
};
