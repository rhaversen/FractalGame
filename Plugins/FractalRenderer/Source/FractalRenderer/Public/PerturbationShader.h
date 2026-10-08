#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"
#include "RenderGraphResources.h"
#include "FractalMath/FractalFormulaTypes.h"

// Thread counts for compute shader
#define NUM_THREADS_PerturbationShader_X 8
#define NUM_THREADS_PerturbationShader_Y 8
#define NUM_THREADS_PerturbationShader_Z 1

/**
 * Compute shader that ray marches the selected fractal with perturbation (see Shaders/FractalRender.ush,
 * Shaders/FractalDE.ush and the per-formula Shaders/Formula*.ush). The CPU supplies:
 *  - a per-view ray basis (world-space ray direction per pixel, built in double precision),
 *  - CameraOffset = (camera - C_ref) / FractalScale, computed in double-double,
 *  - the reference orbit of C_ref with its linear series skip (FractalMath/FractalReferenceOrbit.h).
 * No absolute fractal coordinates reach the GPU, so precision only depends on the visible scale.
 */
class FRACTALRENDERER_API FPerturbationComputeShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPerturbationComputeShader);
	SHADER_USE_PARAMETER_STRUCT(FPerturbationComputeShader, FGlobalShader);

	/** The formula (EFractalType / FractalMath::EFractalFormula). */
	class FFractalTypeDim : SHADER_PERMUTATION_RANGE_INT("FP_FRACTAL_TYPE", 0, FractalMath::FractalFormulaCount);
	/** Mandelbulb with power 8 known at compile time: drops the general-power code and unrolls the powering loops. */
	class FStaticPower8 : SHADER_PERMUTATION_BOOL("FP_STATIC_POWER_8");
	using FPermutationDomain = TShaderPermutationDomain<FFractalTypeDim, FStaticPower8>;

	// Members are ordered so that every FVector3f shares a 16-byte register with the following scalar.
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, OutputOffset)
		SHADER_PARAMETER(FVector3f, RayDir00)
		SHADER_PARAMETER(float, PixelRadiusPerUnitDistance)
		SHADER_PARAMETER(FVector3f, RayDirDX)
		SHADER_PARAMETER(float, FractalScale)
		SHADER_PARAMETER(FVector3f, RayDirDY)
		SHADER_PARAMETER(float, FractalPower)
		SHADER_PARAMETER(FVector3f, CameraOffset)
		SHADER_PARAMETER(float, BailoutRadius)
		SHADER_PARAMETER(FVector3f, ReferenceCenter)
		SHADER_PARAMETER(float, DirectFootprint)
		SHADER_PARAMETER(int32, OrbitLength)
		SHADER_PARAMETER(int32, MaxRaySteps)
		SHADER_PARAMETER(int32, MaxIterations)
		SHADER_PARAMETER(float, MaxRayDistance)
		SHADER_PARAMETER(FVector2f, BackgroundInvExtent)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BackgroundTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, BackgroundSampler)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ReferenceOrbit)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain Permutation(Parameters.PermutationId);
		if (Permutation.Get<FStaticPower8>() && Permutation.Get<FFractalTypeDim>() != static_cast<int32>(FractalMath::EFractalFormula::Mandelbulb))
		{
			return false;
		}
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_PerturbationShader_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_PerturbationShader_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_PerturbationShader_Z);
	}
};
