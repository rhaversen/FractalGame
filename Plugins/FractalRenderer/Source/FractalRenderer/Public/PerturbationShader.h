#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "PerturbationShader.generated.h"

// Thread counts for compute shader
#define NUM_THREADS_PerturbationShader_X 8
#define NUM_THREADS_PerturbationShader_Y 8
#define NUM_THREADS_PerturbationShader_Z 1

/**
 * Parameters for dispatching the perturbation shader
 */
struct FRACTALRENDERER_API FPerturbationShaderDispatchParams
{
	// Thread group counts
	int X;
	int Y;
	int Z;

	// Fractal parameters
	FVector2D Center;          // Center point in complex plane
	
	// Output texture
	UTextureRenderTarget2D* OutputRenderTarget;

	FPerturbationShaderDispatchParams(int x, int y, int z)
		: X(x), Y(y), Z(z)
		, Center(FVector2D::ZeroVector)
		, OutputRenderTarget(nullptr)
	{
	}
};

/**
 * Public interface for the perturbation shader
 */
class FRACTALRENDERER_API FPerturbationShaderInterface
{
public:
	// Executes shader on render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FPerturbationShaderDispatchParams Params,
		TFunction<void()> AsyncCallback
	);

	// Executes shader from game thread
	static void DispatchGameThread(
		FPerturbationShaderDispatchParams Params,
		TFunction<void()> AsyncCallback
	);

	// Dispatches shader from any thread
	static void Dispatch(
		FPerturbationShaderDispatchParams Params,
		TFunction<void()> AsyncCallback
	);
};

/**
 * Compute shader used for fractal rendering
 */
class FRACTALRENDERER_API FPerturbationComputeShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPerturbationComputeShader);
	SHADER_USE_PARAMETER_STRUCT(FPerturbationComputeShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, Center)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FMatrix44f, ClipToView)
		SHADER_PARAMETER(FMatrix44f, ViewToWorld)
		SHADER_PARAMETER(FVector3f, CameraOrigin)
		SHADER_PARAMETER(FVector2f, ViewSize)
		SHADER_PARAMETER(FVector2f, InvViewSize)
		SHADER_PARAMETER(FVector2f, BackgroundExtent)
		SHADER_PARAMETER(FVector2f, BackgroundInvExtent)
		SHADER_PARAMETER(FVector2f, BackgroundViewMin)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BackgroundTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, BackgroundSampler)
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

/**
 * Blueprint-callable async execution node
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPerturbationShader_Complete);

UCLASS()
class FRACTALRENDERER_API UPerturbationShaderLibrary_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	virtual void Activate() override;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "FractalRenderer", WorldContext = "WorldContextObject"))
	static UPerturbationShaderLibrary_AsyncExecution* ExecutePerturbationShader(
		UObject* WorldContextObject,
		UTextureRenderTarget2D* OutputRenderTarget,
		FVector2D Center
	);

	UPROPERTY(BlueprintAssignable)
	FOnPerturbationShader_Complete Completed;

private:
	UTextureRenderTarget2D* OutputRenderTarget;
	FVector2D Center;
};
