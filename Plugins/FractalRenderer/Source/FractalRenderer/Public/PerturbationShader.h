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

/** Parameters shared by the cone pre-pass and the per-pixel march (PerturbationShader.usf, FP_ViewFromParameters). */
BEGIN_SHADER_PARAMETER_STRUCT(FFractalViewParameters, FRACTALRENDERER_API)
	// Members are ordered so that every FVector3f shares a 16-byte register with the following scalar.
	SHADER_PARAMETER(FIntPoint, OutputSize)
	SHADER_PARAMETER(FVector2f, PixelStep)
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
	SHADER_PARAMETER(int32, HazeBase)
	SHADER_PARAMETER(float, MaxRayDistance)
	SHADER_PARAMETER(int32, ConeTile)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ReferenceOrbit)
END_SHADER_PARAMETER_STRUCT()

/** Permutations shared by both passes: the formula, and the Mandelbulb with its classic power 8 as a constant. */
class FFractalTypeDim : SHADER_PERMUTATION_RANGE_INT("FP_FRACTAL_TYPE", 0, FractalMath::FractalFormulaCount);
class FFractalStaticPower8 : SHADER_PERMUTATION_BOOL("FP_STATIC_POWER_8");
using FFractalPermutationDomain = TShaderPermutationDomain<FFractalTypeDim, FFractalStaticPower8>;

inline bool ShouldCompileFractalPermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	const FFractalPermutationDomain Permutation(Parameters.PermutationId);
	if (Permutation.Get<FFractalStaticPower8>() && Permutation.Get<FFractalTypeDim>() != static_cast<int32>(FractalMath::EFractalFormula::Mandelbulb))
	{
		return false;
	}
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

/**
 * Compute shader that ray marches the selected fractal with perturbation (see Shaders/FractalRender.ush,
 * Shaders/FractalDE.ush and the per-formula Shaders/Formula*.ush). The CPU supplies:
 *  - a per-view ray basis (world-space ray direction per pixel, built in double precision),
 *  - CameraOffset = (camera - C_ref) / FractalScale, computed in double-double,
 *  - the reference orbit of C_ref with its linear series skip (FractalMath/FractalReferenceOrbit.h),
 *  - where each tile's rays can start (FFractalConeShader).
 * No absolute fractal coordinates reach the GPU, so precision only depends on the visible scale.
 */
class FRACTALRENDERER_API FPerturbationComputeShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPerturbationComputeShader);
	SHADER_USE_PARAMETER_STRUCT(FPerturbationComputeShader, FGlobalShader);

	using FPermutationDomain = FFractalPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FFractalViewParameters, View)
		SHADER_PARAMETER(FIntPoint, OutputOffset)
		SHADER_PARAMETER(FIntPoint, ViewOffset)
		SHADER_PARAMETER(FVector2f, BackgroundInvExtent)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BackgroundTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, BackgroundSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ConeStart)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileFractalPermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_PerturbationShader_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_PerturbationShader_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_PerturbationShader_Z);
	}
};

/**
 * Cone pre-pass: one ray per ConeTile x ConeTile block of marched pixels, widened to a cone that contains all of their rays,
 * finds how far all of them can start (FP_ConeMarch). The empty space in front of the fractal is then traversed once per
 * tile instead of once per pixel.
 */
class FRACTALRENDERER_API FFractalConeShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFractalConeShader);
	SHADER_USE_PARAMETER_STRUCT(FFractalConeShader, FGlobalShader);

	using FPermutationDomain = FFractalPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FFractalViewParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, ConeOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileFractalPermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADS_X"), NUM_THREADS_PerturbationShader_X);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), NUM_THREADS_PerturbationShader_Y);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), NUM_THREADS_PerturbationShader_Z);
	}
};

/**
 * Bilinear upscale of the fractal marched at reduced resolution (adaptive resolution) into the view rect of the
 * output texture.
 */
class FRACTALRENDERER_API FFractalUpscaleShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFractalUpscaleShader);
	SHADER_USE_PARAMETER_STRUCT(FFractalUpscaleShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, OutputOffset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
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
