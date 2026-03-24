#include "FractalSceneViewExtension.h"
#include "SceneView.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "PixelShaderUtils.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RHIStaticStates.h"
#include "PerturbationShader.h"
#include "MandelbulbOrbitGenerator.h"
#include "RHICommandList.h"

DEFINE_LOG_CATEGORY_STATIC(LogFractalViewExtension, Log, All);

FFractalSceneViewExtension::FFractalSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
	, CurrentReferenceCenter(FVector3d::ZeroVector)
	, CurrentOrbitLength(0)
	, bOrbitHasDerivatives(false)
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

void FFractalSceneViewExtension::SetReferenceOrbit(const FReferenceOrbit& InOrbit)
{
	FScopeLock Lock(&OrbitMutex);
	
	if (InOrbit.IsValid())
	{
		// Convert orbit to float format for GPU upload
		FMandelbulbOrbitGenerator::BuildOrbitBuffer(InOrbit, OrbitBufferData);
		CurrentReferenceCenter = InOrbit.ReferenceCenter;
		CurrentOrbitLength = InOrbit.GetLength();
		bOrbitHasDerivatives = InOrbit.HasDerivatives();
		
		UE_LOG(LogFractalViewExtension, Verbose, 
			TEXT("Orbit updated: %d points, Center=(%.6f, %.6f, %.6f)"),
			CurrentOrbitLength,
			CurrentReferenceCenter.X, CurrentReferenceCenter.Y, CurrentReferenceCenter.Z
		);
	}
	else
	{
		UE_LOG(LogFractalViewExtension, Warning, TEXT("Invalid orbit provided"));
		OrbitBufferData.Empty();
		CurrentOrbitLength = 0;
		bOrbitHasDerivatives = false;
	}
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

	// Determine output dimensions and bail early if the view rect is invalid
	const FIntPoint OutputExtent = SceneColor.ViewRect.Size();
	if (OutputExtent.X <= 0 || OutputExtent.Y <= 0)
	{
		UE_LOG(LogFractalViewExtension, Verbose, TEXT("RenderFractal skipped: invalid output extent %dx%d"), OutputExtent.X, OutputExtent.Y);
		return SceneColor;
	}

	// Create output texture matching scene color format
	FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
	OutputDesc.Format = PF_FloatRGBA;
	OutputDesc.ClearValue = FClearValueBinding::Black;
	OutputDesc.Flags |= TexCreate_UAV;
	
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("FractalOutput"));

	// Allocate shader parameters
	auto* PassParameters = GraphBuilder.AllocParameters<FPerturbationComputeShader::FParameters>();
	PassParameters->Center = FVector2f(CurrentParams.Center);
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

	// Create and upload orbit buffer
	FRDGBufferRef OrbitBuffer = nullptr;
	FRDGBufferSRVRef OrbitBufferSRV = nullptr;
	TArray<FPackedOrbitSample> LocalOrbitBufferData;
	FVector3d LocalReferenceCenter;
	int32 LocalOrbitLength = 0;
	bool bLocalHasDerivatives = false;
	
	{
		FScopeLock Lock(&OrbitMutex);
		LocalOrbitBufferData = OrbitBufferData;
		LocalReferenceCenter = CurrentReferenceCenter;
		LocalOrbitLength = CurrentOrbitLength;
		bLocalHasDerivatives = bOrbitHasDerivatives;
	}
	(void)bLocalHasDerivatives;
	
	if (LocalOrbitLength > 0 && LocalOrbitBufferData.Num() > 0)
	{
		OrbitBuffer = CreateOrbitBuffer(GraphBuilder, LocalOrbitBufferData);
		OrbitBufferSRV = GraphBuilder.CreateSRV(OrbitBuffer);
		PassParameters->ReferenceOrbitBuffer = OrbitBufferSRV;
		PassParameters->ReferenceCenter = FVector3f(LocalReferenceCenter);
		PassParameters->OrbitLength = bLocalHasDerivatives ? LocalOrbitLength : 0;
	}
	else
	{
		// No orbit data available, create dummy buffer with a single zero sample
		TArray<FPackedOrbitSample> DummySamples;
		DummySamples.AddDefaulted(1);
		OrbitBuffer = CreateOrbitBuffer(GraphBuilder, DummySamples);
		OrbitBufferSRV = GraphBuilder.CreateSRV(OrbitBuffer);
		PassParameters->ReferenceOrbitBuffer = OrbitBufferSRV;
		PassParameters->ReferenceCenter = FVector3f::ZeroVector;
		PassParameters->OrbitLength = 0;
	}

	const FIntVector GroupCount(
		FMath::DivideAndRoundUp(OutputExtent.X, NUM_THREADS_PerturbationShader_X),
		FMath::DivideAndRoundUp(OutputExtent.Y, NUM_THREADS_PerturbationShader_Y),
		1);

	const bool bImmediateMode = GraphBuilder.IsImmediateMode();
	if (GroupCount.X <= 0 || GroupCount.Y <= 0)
	{
		UE_LOG(LogFractalViewExtension, Warning, TEXT("RenderFractal skipped: invalid dispatch group count (%d, %d, %d)"), GroupCount.X, GroupCount.Y, GroupCount.Z);
		return SceneColor;
	}

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("RenderFractal"),
		ComputeShader,
		PassParameters,
		GroupCount
	);

	return FScreenPassTexture(OutputTexture, SceneColor.ViewRect);
}

FRDGBufferRef FFractalSceneViewExtension::CreateOrbitBuffer(
	FRDGBuilder& GraphBuilder,
	const TArray<FPackedOrbitSample>& OrbitData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FFractalSceneViewExtension::CreateOrbitBuffer);

	if (OrbitData.Num() == 0)
	{
		UE_LOG(LogFractalViewExtension, Warning, TEXT("CreateOrbitBuffer: Empty orbit data"));
		return nullptr;
	}

	const uint32 NumElements = static_cast<uint32>(OrbitData.Num());
	const uint64 DataSizeBytes = static_cast<uint64>(NumElements) * sizeof(FPackedOrbitSample);

	FRDGBufferRef Buffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("ReferenceOrbitBuffer"),
		sizeof(FPackedOrbitSample),
		NumElements,
		OrbitData.GetData(),
		DataSizeBytes,
		ERDGInitialDataFlags::None);

	UE_LOG(LogFractalViewExtension, VeryVerbose,
		TEXT("Created orbit buffer: %u samples, %.2f KB"),
		NumElements,
		DataSizeBytes / 1024.0);

	return Buffer;
}

