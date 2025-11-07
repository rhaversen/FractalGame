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

namespace
{
BEGIN_SHADER_PARAMETER_STRUCT(FUploadOrbitDataParameters, )
	RDG_TEXTURE_ACCESS(OrbitTexture, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()
}

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
		FMandelbulbOrbitGenerator::ConvertOrbitToFloat(InOrbit, OrbitPositionData, OrbitDerivativeData);
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
		OrbitPositionData.Empty();
		OrbitDerivativeData.Empty();
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

	// Create and upload orbit texture
	FRDGTextureRef OrbitTexture = nullptr;
	TArray<FVector4f> LocalOrbitPositionData;
	TArray<FVector4f> LocalOrbitDerivativeData;
	FVector3d LocalReferenceCenter;
	int32 LocalOrbitLength = 0;
	bool bLocalHasDerivatives = false;
	
	{
		FScopeLock Lock(&OrbitMutex);
		LocalOrbitPositionData = OrbitPositionData;
		LocalOrbitDerivativeData = OrbitDerivativeData;
		LocalReferenceCenter = CurrentReferenceCenter;
		LocalOrbitLength = CurrentOrbitLength;
		bLocalHasDerivatives = bOrbitHasDerivatives;
	}
	(void)LocalOrbitDerivativeData; // Placeholder until derivative textures are uploaded
	(void)bLocalHasDerivatives; // Derivative sampling will be hooked up in a later task
	
	if (LocalOrbitLength > 0 && LocalOrbitPositionData.Num() > 0)
	{
		OrbitTexture = CreateOrbitTexture(GraphBuilder, LocalOrbitPositionData);
		PassParameters->ReferenceOrbitTexture = OrbitTexture;
		PassParameters->OrbitSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->ReferenceCenter = FVector3f(LocalReferenceCenter);
		PassParameters->OrbitLength = LocalOrbitLength;
	}
	else
	{
		// No orbit data available, create dummy 1x1 texture
		FRDGTextureDesc DummyDesc = FRDGTextureDesc::Create2D(
			FIntPoint(1, 1),
			PF_A32B32G32R32F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV
		);
		OrbitTexture = GraphBuilder.CreateTexture(DummyDesc, TEXT("DummyOrbitTexture"));
		FRDGTextureUAVRef DummyUAV = GraphBuilder.CreateUAV(OrbitTexture);
		AddClearUAVPass(GraphBuilder, DummyUAV, FLinearColor::Black);
		
		PassParameters->ReferenceOrbitTexture = OrbitTexture;
		PassParameters->OrbitSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
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

FRDGTextureRef FFractalSceneViewExtension::CreateOrbitTexture(
	FRDGBuilder& GraphBuilder,
	const TArray<FVector4f>& OrbitData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FFractalSceneViewExtension::CreateOrbitTexture);
	
	if (OrbitData.Num() == 0)
	{
		UE_LOG(LogFractalViewExtension, Warning, TEXT("CreateOrbitTexture: Empty orbit data"));
		return nullptr;
	}
	
	const int32 OrbitLength = OrbitData.Num();
	
	// Create 1D texture (Width = OrbitLength, Height = 1)
	// Format: PF_A32B32G32R32F (128-bit per texel, RGBA float)
	FRDGTextureDesc OrbitDesc = FRDGTextureDesc::Create2D(
		FIntPoint(OrbitLength, 1),
		PF_A32B32G32R32F,
		FClearValueBinding::Black,
		TexCreate_ShaderResource
	);
	
	FRDGTextureRef OrbitTexture = GraphBuilder.CreateTexture(OrbitDesc, TEXT("ReferenceOrbitTexture"));
	
	// Upload orbit data using an RDG copy pass and render-graph managed backing storage
	FUploadOrbitDataParameters* UploadParams = GraphBuilder.AllocParameters<FUploadOrbitDataParameters>();
	UploadParams->OrbitTexture = OrbitTexture;
	
	const int32 DataSizeBytes = OrbitLength * sizeof(FVector4f);
	const uint32 RowPitchBytes = static_cast<uint32>(OrbitLength * sizeof(FVector4f));

	FRDGUploadData<FVector4f> UploadData(GraphBuilder, OrbitLength);
	FMemory::Memcpy(UploadData.GetData(), OrbitData.GetData(), DataSizeBytes);

	const uint8* UploadDataPtr = reinterpret_cast<const uint8*>(UploadData.GetData());
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("UploadOrbitData"),
		UploadParams,
		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
		[OrbitTexture, UploadDataPtr, OrbitLength, RowPitchBytes](FRHICommandList& RHICmdList)
		{
			// Get the RHI texture
			FRHITexture* TextureRHI = OrbitTexture->GetRHI();
			
			// Define the region to update
			FUpdateTextureRegion2D Region(0, 0, 0, 0, OrbitLength, 1);
			
			// Update texture with orbit data
			RHICmdList.UpdateTexture2D(
				TextureRHI,
				0, // Mip level
				Region,
				RowPitchBytes,
				UploadDataPtr
			);
		}
	);
	
	UE_LOG(LogFractalViewExtension, VeryVerbose, 
		TEXT("Created orbit texture: %dx%d, %d points, %.2f KB"),
		OrbitLength, 1, OrbitLength, DataSizeBytes / 1024.0f
	);
	
	return OrbitTexture;
}
