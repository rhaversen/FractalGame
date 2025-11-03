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
	, Center(FVector2D::ZeroVector)
	, bEnabled(true)
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

	if (PassId == EPostProcessingPass::Tonemap && bEnabled)
	{
		static_cast<void>(View);
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(
			this, &FFractalSceneViewExtension::RenderFractal_RenderThread));
	}
}

FScreenPassTexture FFractalSceneViewExtension::RenderFractal_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	check(IsInRenderingThread());

	// Get the input scene color slice that the post-process pass provides
	const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);

	if (!SceneColorSlice.IsValid())
	{
		return FScreenPassTexture(SceneColorSlice);
	}

	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice);

	if (!SceneColor.IsValid())
	{
		return FScreenPassTexture(SceneColorSlice);
	}

	// Get shader from global shader map
	TShaderMapRef<FPerturbationComputeShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	if (!ComputeShader.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FractalSceneViewExtension: Shader not valid"));
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
	PassParameters->Center = FVector2f(Center);
	const FIntPoint OutputExtent = SceneColor.ViewRect.Size();
	PassParameters->OutputSize = OutputExtent;

	const FRDGTextureDesc& SceneColorDesc = SceneColor.Texture->Desc;
	const FIntPoint TextureExtent = SceneColorDesc.Extent;
	const FIntPoint ViewMin = SceneColor.ViewRect.Min;

	const FViewMatrices& ViewMatrices = View.ViewMatrices;
	PassParameters->ClipToView = FMatrix44f(ViewMatrices.GetInvProjectionMatrix());
	PassParameters->ViewToWorld = FMatrix44f(ViewMatrices.GetInvViewMatrix());
	PassParameters->CameraOrigin = FVector3f(ViewMatrices.GetViewOrigin());
	const FVector2f ViewSizeVector(static_cast<float>(OutputExtent.X), static_cast<float>(OutputExtent.Y));
	PassParameters->ViewSize = ViewSizeVector;
	PassParameters->InvViewSize = FVector2f(
		OutputExtent.X > 0 ? 1.0f / static_cast<float>(OutputExtent.X) : 0.0f,
		OutputExtent.Y > 0 ? 1.0f / static_cast<float>(OutputExtent.Y) : 0.0f);
	PassParameters->BackgroundExtent = FVector2f(static_cast<float>(TextureExtent.X), static_cast<float>(TextureExtent.Y));
	PassParameters->BackgroundInvExtent = FVector2f(
		TextureExtent.X > 0 ? 1.0f / static_cast<float>(TextureExtent.X) : 0.0f,
		TextureExtent.Y > 0 ? 1.0f / static_cast<float>(TextureExtent.Y) : 0.0f);
	PassParameters->BackgroundViewMin = FVector2f(static_cast<float>(ViewMin.X), static_cast<float>(ViewMin.Y));
	PassParameters->BackgroundTexture = SceneColor.Texture;
	PassParameters->BackgroundSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

	// Calculate thread group count (8x8 groups)
	FIntVector GroupCount = FIntVector(
		FMath::DivideAndRoundUp(OutputExtent.X, NUM_THREADS_PerturbationShader_X),
		FMath::DivideAndRoundUp(OutputExtent.Y, NUM_THREADS_PerturbationShader_Y),
		1
	);

	// Add compute pass to render graph
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("FractalSceneViewExtension"),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
		}
	);

	// Return the fractal output as the new scene color
	return FScreenPassTexture(OutputTexture, SceneColor.ViewRect);
}
