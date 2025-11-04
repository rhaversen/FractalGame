#include "FractalSceneViewExtension.h"
#include "SceneView.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PerturbationShader.h"
#include "PixelShaderUtils.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RHIStaticStates.h"

FFractalSceneViewExtension::FFractalSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FFractalSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass PassId,
	const FSceneView& View,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	// Insert our fractal rendering after tonemapping
	if (!bIsPassEnabled)
	{
		return;
	}

	if (PassId == EPostProcessingPass::Tonemap)
	{
		// The FScreenPassTexture is a temporary texture that is only valid for the duration of the render pass.
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FFractalSceneViewExtension::RenderFractal_RenderThread));
	}
}

void FFractalSceneViewExtension::SetFractalParameters(const FFractalParameter& InParams)
{
	FScopeLock Lock(&ParameterMutex);
	FractalParameters = InParams;
}

FScreenPassTexture FFractalSceneViewExtension::RenderFractal_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	check(IsInRenderingThread());

	FFractalParameter CurrentParams;
	{
		FScopeLock Lock(&ParameterMutex);
		CurrentParams = FractalParameters;
	}

	if (!CurrentParams.bEnabled)
	{
		return FScreenPassTexture(Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	}

	// Get the input scene color slice that the post-process pass provides
	const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);

	if (!SceneColorSlice.IsValid())
	{
		return FScreenPassTexture(SceneColorSlice);
	}

	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice);

	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	// Get shader from global shader map
	TShaderMapRef<FPerturbationComputeShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	if (!ComputeShader.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("FPerturbationComputeShader is not valid!"));
		return SceneColor;
	}

	// Create output texture matching scene color format
	FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
	OutputDesc.Reset();
	OutputDesc.Format = PF_FloatRGBA;
	OutputDesc.ClearValue = FClearValueBinding::Black;
	OutputDesc.Flags |= TexCreate_UAV;
	
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("FractalOutput"));

	// Allocate shader parameters
	auto* PassParameters = GraphBuilder.AllocParameters<FPerturbationComputeShader::FParameters>();
	PassParameters->Center = FVector2f(CurrentParams.Center);
	const FIntPoint OutputExtent = SceneColor.ViewRect.Size();
	PassParameters->OutputSize = OutputExtent;
	PassParameters->Zoom = CurrentParams.Zoom;
	PassParameters->MaxRaySteps = CurrentParams.MaxRaySteps;
	PassParameters->MaxRayDistance = CurrentParams.MaxRayDistance;
	PassParameters->MaxIterations = CurrentParams.MaxIterations;
	PassParameters->BailoutRadius = CurrentParams.BailoutRadius;
	PassParameters->MinIterations = CurrentParams.MinIterations;
	PassParameters->ConvergenceFactor = CurrentParams.ConvergenceFactor;
	PassParameters->FractalPower = CurrentParams.FractalPower;

	const FRDGTextureDesc& SceneColorDesc = SceneColor.Texture->Desc;
	const FIntPoint TextureExtent = SceneColorDesc.Extent;
	const FIntPoint ViewMin = SceneColor.ViewRect.Min;
	const FVector2f InvViewSize = FVector2f(1.0f / SceneColor.ViewRect.Width(), 1.0f / SceneColor.ViewRect.Height());

	PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);
	PassParameters->BackgroundTexture = SceneColor.Texture;
	PassParameters->BackgroundSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->BackgroundExtent = FVector2f(TextureExtent.X, TextureExtent.Y);
	PassParameters->BackgroundInvExtent = FVector2f(1.0f / TextureExtent.X, 1.0f / TextureExtent.Y);
	PassParameters->BackgroundViewMin = FVector2f(ViewMin.X, ViewMin.Y);
	PassParameters->ClipToView = FMatrix44f(View.ViewMatrices.GetInvProjectionMatrix());
	PassParameters->ViewToWorld = FMatrix44f(View.ViewMatrices.GetInvViewMatrix());
	PassParameters->CameraOrigin = (FVector3f)View.ViewMatrices.GetViewOrigin();
	PassParameters->ViewSize = FVector2f(SceneColor.ViewRect.Width(), SceneColor.ViewRect.Height());
	PassParameters->InvViewSize = InvViewSize;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("RenderFractal"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(OutputExtent, FIntPoint(NUM_THREADS_PerturbationShader_X, NUM_THREADS_PerturbationShader_Y))
	);

	return FScreenPassTexture(OutputTexture, SceneColor.ViewRect);
}
