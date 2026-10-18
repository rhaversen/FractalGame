#include "FractalSceneViewExtension.h"
#include "SceneView.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RHIStaticStates.h"
#include "PerturbationShader.h"
#include "FractalMath/FractalCamera.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogFractalViewExtension, Log, All);

// Targeted using-declarations (not a using-directive: Unreal's unity builds share one translation unit).
using FractalMath::BuildRayBasis;
using FractalMath::FDDVec3;
using FractalMath::FDVec3;
using FractalMath::FFractalRayBasis;
using FractalMath::InvertMatrix4;
using FractalMath::ToDoubleVec;

namespace
{
	FVector3f ToVector3f(double X, double Y, double Z)
	{
		return FVector3f(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
	}

	FVector3f ToVector3f(const double V[3])
	{
		return ToVector3f(V[0], V[1], V[2]);
	}
}

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
	// One reference serves the player's view; captures (reflection, planar, scene capture) would fight over it.
	if (bIsPassEnabled && PassId == EPostProcessingPass::Tonemap && !View.bIsSceneCapture && !View.bIsReflectionCapture && !View.bIsPlanarReflection)
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FFractalSceneViewExtension::RenderFractal_RenderThread));
	}
}

void FFractalSceneViewExtension::SetFractalParameters(const FFractalParameter& InParams)
{
	FScopeLock Lock(&StateMutex);
	FractalParameters = InParams;
}

void FFractalSceneViewExtension::SetCameraMapping(const FFractalCameraMapping& InMapping)
{
	FScopeLock Lock(&StateMutex);
	CameraMapping = InMapping;
}

FScreenPassTexture FFractalSceneViewExtension::RenderFractal_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	check(IsInRenderingThread());

	FFractalParameter Params;
	FFractalCameraMapping Mapping;
	{
		FScopeLock Lock(&StateMutex);
		Params = FractalParameters;
		Mapping = CameraMapping;
	}

	const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	if (!Params.bEnabled || !SceneColorSlice.IsValid() || Params.FractalType >= EFractalType::Count)
	{
		return FScreenPassTexture(SceneColorSlice);
	}

	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice);
	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	const FIntRect ViewRect = SceneColor.ViewRect;
	const FIntPoint OutputExtent = ViewRect.Size();
	if (OutputExtent.X <= 0 || OutputExtent.Y <= 0)
	{
		return SceneColor;
	}

	// --- Camera: world -> fractal space (double-double) and per-pixel ray basis (double) ---
	const FViewMatrices& ViewMatrices = View.ViewMatrices;
	const FVector3d CameraWorld = ViewMatrices.GetViewOrigin();
	const FDDVec3 CameraFractal = Mapping.WorldToFractal(CameraWorld);

	// Unjittered projection: the fractal is drawn after TAA/TSR, so sub-pixel jitter would only shimmer.
	double Projection[4][4];
	double InvProjection[4][4];
	const FMatrix ProjectionNoAA = ViewMatrices.GetProjectionNoAAMatrix();
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Col = 0; Col < 4; ++Col)
		{
			Projection[Row][Col] = ProjectionNoAA.M[Row][Col];
		}
	}
	if (!InvertMatrix4(Projection, InvProjection))
	{
		return SceneColor;
	}
	const FMatrix InvView = ViewMatrices.GetInvViewMatrix();
	double ViewToWorld[3][3];
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Col = 0; Col < 3; ++Col)
		{
			ViewToWorld[Row][Col] = InvView.M[Row][Col];
		}
	}
	const FFractalRayBasis Basis = BuildRayBasis(InvProjection, ViewToWorld, OutputExtent.X, OutputExtent.Y);

	// --- Perturbation reference (centre-ray march + double-double orbit) ---
	const FractalMath::EFractalFormula Formula = ToFractalFormula(Params.FractalType);
	// The ray limit is a fractal-space distance; in world units it grows as the zoom deepens.
	const double MaxRayDistanceWorld = FMath::Min(static_cast<double>(Params.MaxRayDistance) / Mapping.Scale, 1.0e36);
	FFractalReferenceRequest Request;
	Request.Camera = CameraFractal;
	Request.Forward = FVector3d(InvView.M[2][0], InvView.M[2][1], InvView.M[2][2]).GetSafeNormal();
	Request.Scale = Mapping.Scale;
	Request.PixelRadiusPerUnitDistance = Basis.PixelRadiusPerUnitDistance;
	Request.Formula = Formula;
	Request.Params.Power = Params.FractalPower;
	Request.Params.Bailout = Params.BailoutRadius;
	Request.MaxRaySteps = Params.MaxRaySteps;
	Request.MaxRayDistance = MaxRayDistanceWorld;
	int32 HazeBase = 0;
	{
		// Distance to the surface (CPU estimate of the formula being drawn): decides when the reference is stale and how
		// many iterations the view needs. For escape-time formulas the camera's orbit may run long before it escapes
		// next to the surface (the estimate stops at the escape, so the cap costs nothing for a camera outside the set).
		const bool bEscapeTime = FractalMath::FormulaFamily(Formula) == FractalMath::EFractalFamily::EscapeTime;
		FractalMath::FFormulaParams EstimateParams;
		EstimateParams.Power = Params.FractalPower;
		EstimateParams.Bailout = Params.BailoutRadius;
		EstimateParams.MaxIterations = bEscapeTime ? FractalMath::MaxOrbitIterations : FMath::Max(Params.MaxIterations, 2);
		const FractalMath::FDistanceEstimate CameraDE = FractalMath::DistanceEstimateAtScale(Formula, CameraFractal, Mapping.Scale, EstimateParams);
		Request.CameraDistanceWorld = CameraDE.SafeDistance() / Mapping.Scale;
		const double SurfaceDistanceFractal = FMath::Max(Request.CameraDistanceWorld * Mapping.Scale, Mapping.Scale);
		int32 Iterations = FractalMath::ComputeIterationBudget(Formula, Params.FractalPower, Params.MaxIterations, Params.BailoutRadius,
			SurfaceDistanceFractal * Basis.PixelRadiusPerUnitDistance);
		if (bEscapeTime && CameraDE.bEscaped)
		{
			// Next to the surface: size the view from the camera's own orbit (its pixels need more iterations than it)
			// and measure the haze from its escape count, so only detail finer than the pixels fades.
			const int32 OrbitBudget = FractalMath::ComputeOrbitIterationBudget(CameraDE.Iterations, CameraDE.SafeDistance(), Basis.PixelRadiusPerUnitDistance);
			if (OrbitBudget > Iterations)
			{
				Iterations = OrbitBudget;
				HazeBase = CameraDE.Iterations;
			}
		}
		Request.Params.MaxIterations = Iterations;
	}
	{
		double Fan[FractalMath::ReferenceFanCount][3];
		FractalMath::BuildReferenceFan(Basis, OutputExtent.X, OutputExtent.Y, Fan);
		Request.Directions.SetNumUninitialized(FractalMath::ReferenceFanCount);
		for (int32 I = 0; I < FractalMath::ReferenceFanCount; ++I)
		{
			Request.Directions[I] = FVector3d(Fan[I][0], Fan[I][1], Fan[I][2]);
		}
	}
	const TSharedPtr<const FFractalReferenceData, ESPMode::ThreadSafe> Reference = ReferenceManager.Update(Request);

	const FDVec3 CameraFractalD = ToDoubleVec(CameraFractal);
	FVector3f CameraOffset = FVector3f::ZeroVector;
	FVector3f ReferenceCenter = ToVector3f(CameraFractalD.X, CameraFractalD.Y, CameraFractalD.Z);
	int32 OrbitLength = 0;
	if (Reference.IsValid() && Reference->Formula == Formula && Reference->OrbitLength >= 2)
	{
		// (camera - C_ref) in double-double, then expressed in world units: this is the only place where the
		// absolute fractal position enters, and it is exact regardless of how deep the camera is.
		const FDVec3 Offset = ToDoubleVec(CameraFractal - Reference->Center);
		CameraOffset = ToVector3f(Offset.X / Mapping.Scale, Offset.Y / Mapping.Scale, Offset.Z / Mapping.Scale);
		const FDVec3 RefCenter = ToDoubleVec(Reference->Center);
		ReferenceCenter = ToVector3f(RefCenter.X, RefCenter.Y, RefCenter.Z);
		OrbitLength = Reference->OrbitLength;
	}

	// --- GPU resources ---
	// The orbit is a few KB (6-7 float4 per iteration); uploading it every frame is cheaper than tracking
	// buffer lifetimes across frames and views.
	FRDGBufferRef OrbitBuffer;
	if (OrbitLength > 0)
	{
		OrbitBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("FractalReferenceOrbit"), static_cast<uint32>(sizeof(FVector4f)),
			static_cast<uint32>(Reference->Orbit.Num()), Reference->Orbit.GetData(), static_cast<uint64>(Reference->Orbit.Num()) * sizeof(FVector4f));
	}
	else
	{
		// Unused by the shader when OrbitLength == 0, but the SRV must exist.
		const FVector4f Empty(0.0f, 0.0f, 0.0f, 0.0f);
		OrbitBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("FractalReferenceOrbit"), static_cast<uint32>(sizeof(FVector4f)), 1u, &Empty, static_cast<uint64>(sizeof(FVector4f)));
	}

	FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
	OutputDesc.Format = PF_FloatRGBA;
	OutputDesc.ClearValue = FClearValueBinding::Black;
	OutputDesc.Flags |= TexCreate_UAV;
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("FractalOutput"));

	const FIntPoint TextureExtent = SceneColor.Texture->Desc.Extent;

	// Adaptive resolution: the fractal is marched at a fraction of the view size (cost follows the pixel count) and
	// upscaled bilinearly into the view rect. At full resolution it is marched straight into the output.
	const float ResolutionScale = FMath::Clamp(Params.ResolutionScale, 0.1f, 1.0f);
	const bool bReducedResolution = ResolutionScale < 0.999f;
	const FIntPoint MarchExtent = bReducedResolution
		? FIntPoint(FMath::Max(1, FMath::CeilToInt(OutputExtent.X * ResolutionScale)), FMath::Max(1, FMath::CeilToInt(OutputExtent.Y * ResolutionScale)))
		: OutputExtent;
	FRDGTextureRef MarchTexture = OutputTexture;
	if (bReducedResolution)
	{
		FRDGTextureDesc MarchDesc = FRDGTextureDesc::Create2D(MarchExtent, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		MarchTexture = GraphBuilder.CreateTexture(MarchDesc, TEXT("FractalMarchOutput"));
	}

	// Marched pixels per cone tile side (measured in Web/bench: 8 traverses the empty space once per 64 pixels).
	constexpr int32 ConeTile = 8;

	// Parameters shared by the cone pre-pass and the march.
	FFractalViewParameters ViewParameters;
	ViewParameters.OutputSize = MarchExtent;
	ViewParameters.PixelStep = FVector2f(static_cast<float>(OutputExtent.X) / MarchExtent.X, static_cast<float>(OutputExtent.Y) / MarchExtent.Y);
	ViewParameters.RayDir00 = ToVector3f(Basis.DirPixel00);
	ViewParameters.RayDirDX = ToVector3f(Basis.DirDX);
	ViewParameters.RayDirDY = ToVector3f(Basis.DirDY);
	ViewParameters.PixelRadiusPerUnitDistance = static_cast<float>(Basis.PixelRadiusPerUnitDistance);
	ViewParameters.CameraOffset = CameraOffset;
	ViewParameters.FractalScale = static_cast<float>(Mapping.Scale);
	ViewParameters.ReferenceCenter = ReferenceCenter;
	ViewParameters.DirectFootprint = Params.DirectEvaluationFootprint;
	ViewParameters.OrbitLength = OrbitLength;
	ViewParameters.MaxRaySteps = Params.MaxRaySteps;
	ViewParameters.MaxIterations = Request.Params.MaxIterations;
	ViewParameters.HazeBase = HazeBase;
	ViewParameters.MaxRayDistance = static_cast<float>(MaxRayDistanceWorld);
	ViewParameters.FractalPower = Params.FractalPower;
	ViewParameters.BailoutRadius = Params.BailoutRadius;
	ViewParameters.ConeTile = ConeTile;
	ViewParameters.ReferenceOrbit = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(OrbitBuffer));

	FFractalPermutationDomain Permutation;
	Permutation.Set<FFractalTypeDim>(static_cast<int32>(Params.FractalType));
	Permutation.Set<FFractalStaticPower8>(Params.FractalType == EFractalType::Mandelbulb && Params.FractalPower == 8.0f);
	TShaderMapRef<FPerturbationComputeShader> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()), Permutation);
	TShaderMapRef<FFractalConeShader> ConeShader(GetGlobalShaderMap(View.GetFeatureLevel()), Permutation);
	if (!ComputeShader.IsValid() || !ConeShader.IsValid())
	{
		UE_LOG(LogFractalViewExtension, Error, TEXT("Fractal compute shaders are not valid"));
		return SceneColor;
	}

	// Cone pre-pass: where each tile of ConeTile x ConeTile marched pixels can start marching.
	const FIntPoint ConeExtent(FMath::DivideAndRoundUp(MarchExtent.X, ConeTile), FMath::DivideAndRoundUp(MarchExtent.Y, ConeTile));
	FRDGTextureRef ConeTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(ConeExtent, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), TEXT("FractalConeStart"));
	{
		FFractalConeShader::FParameters* ConeParameters = GraphBuilder.AllocParameters<FFractalConeShader::FParameters>();
		ConeParameters->View = ViewParameters;
		ConeParameters->ConeOutput = GraphBuilder.CreateUAV(ConeTexture);
		const FIntVector ConeGroups(
			FMath::DivideAndRoundUp(ConeExtent.X, NUM_THREADS_PerturbationShader_X),
			FMath::DivideAndRoundUp(ConeExtent.Y, NUM_THREADS_PerturbationShader_Y),
			1);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("FractalCone %dx%d", ConeExtent.X, ConeExtent.Y), ConeShader, ConeParameters, ConeGroups);
	}

	FPerturbationComputeShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FPerturbationComputeShader::FParameters>();
	PassParameters->View = ViewParameters;
	PassParameters->OutputOffset = bReducedResolution ? FIntPoint::ZeroValue : ViewRect.Min;
	PassParameters->ViewOffset = ViewRect.Min;
	PassParameters->BackgroundInvExtent = FVector2f(1.0f / TextureExtent.X, 1.0f / TextureExtent.Y);
	PassParameters->BackgroundTexture = SceneColor.Texture;
	PassParameters->BackgroundSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->ConeStart = ConeTexture;
	PassParameters->OutputTexture = GraphBuilder.CreateUAV(MarchTexture);

	const FIntVector GroupCount(
		FMath::DivideAndRoundUp(MarchExtent.X, NUM_THREADS_PerturbationShader_X),
		FMath::DivideAndRoundUp(MarchExtent.Y, NUM_THREADS_PerturbationShader_Y),
		1);

	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RenderFractal %dx%d", MarchExtent.X, MarchExtent.Y), ComputeShader, PassParameters, GroupCount);

	if (bReducedResolution)
	{
		TShaderMapRef<FFractalUpscaleShader> UpscaleShader(GetGlobalShaderMap(View.GetFeatureLevel()));
		FFractalUpscaleShader::FParameters* UpscaleParameters = GraphBuilder.AllocParameters<FFractalUpscaleShader::FParameters>();
		UpscaleParameters->OutputSize = OutputExtent;
		UpscaleParameters->OutputOffset = ViewRect.Min;
		UpscaleParameters->InputTexture = MarchTexture;
		UpscaleParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		UpscaleParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);
		const FIntVector UpscaleGroups(
			FMath::DivideAndRoundUp(OutputExtent.X, NUM_THREADS_PerturbationShader_X),
			FMath::DivideAndRoundUp(OutputExtent.Y, NUM_THREADS_PerturbationShader_Y),
			1);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("UpscaleFractal"), UpscaleShader, UpscaleParameters, UpscaleGroups);
	}

	return FScreenPassTexture(OutputTexture, ViewRect);
}
