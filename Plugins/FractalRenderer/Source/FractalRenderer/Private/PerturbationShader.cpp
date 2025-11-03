#include "PerturbationShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "PixelShaderUtils.h"
#include "ShaderCompilerCore.h"

DECLARE_STATS_GROUP(TEXT("PerturbationShader"), STATGROUP_PerturbationShader, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("PerturbationShader Execute"), STAT_PerturbationShader_Execute, STATGROUP_PerturbationShader);

IMPLEMENT_GLOBAL_SHADER(FPerturbationComputeShader, "/FractalRendererShaders/PerturbationShader.usf", "PerturbationShader", SF_Compute);

void FPerturbationShaderInterface::DispatchRenderThread(
	FRHICommandListImmediate& RHICmdList,
	FPerturbationShaderDispatchParams Params,
	TFunction<void()> AsyncCallback)
{
	if (!Params.OutputRenderTarget)
	{
		UE_LOG(LogTemp, Error, TEXT("PerturbationShader: OutputRenderTarget is null"));
		if (AsyncCallback)
		{
			AsyncCallback();
		}
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	{
		SCOPE_CYCLE_COUNTER(STAT_PerturbationShader_Execute);
		RDG_EVENT_SCOPE(GraphBuilder, "PerturbationShader");
		RDG_GPU_STAT_SCOPE(GraphBuilder, PerturbationShader);

		TShaderMapRef<FPerturbationComputeShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		if (ComputeShader.IsValid())
		{
			FPerturbationComputeShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FPerturbationComputeShader::FParameters>();

			// Set shader parameters
			PassParameters->Center = FVector2f(Params.Center);
			PassParameters->OutputSize = FIntPoint(Params.OutputRenderTarget->SizeX, Params.OutputRenderTarget->SizeY);

			// Get the render target resource
			FTextureRenderTargetResource* RTResource = Params.OutputRenderTarget->GameThread_GetRenderTargetResource();
			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RTResource->GetRenderTargetTexture(), TEXT("PerturbationOutput")));
			PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

			// Calculate thread group count
			FIntVector GroupCount = FIntVector(
				FMath::DivideAndRoundUp(Params.OutputRenderTarget->SizeX, NUM_THREADS_PerturbationShader_X),
				FMath::DivideAndRoundUp(Params.OutputRenderTarget->SizeY, NUM_THREADS_PerturbationShader_Y),
				1
			);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ExecutePerturbationShader"),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
				}
			);
		}
	}

	GraphBuilder.Execute();

	if (AsyncCallback)
	{
		AsyncCallback();
	}
}

void FPerturbationShaderInterface::DispatchGameThread(
	FPerturbationShaderDispatchParams Params,
	TFunction<void()> AsyncCallback)
{
	ENQUEUE_RENDER_COMMAND(PerturbationShaderDispatch)(
		[Params, AsyncCallback](FRHICommandListImmediate& RHICmdList)
		{
			DispatchRenderThread(RHICmdList, Params, AsyncCallback);
		}
	);
}

void FPerturbationShaderInterface::Dispatch(
	FPerturbationShaderDispatchParams Params,
	TFunction<void()> AsyncCallback)
{
	if (IsInRenderingThread())
	{
		DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
	}
	else
	{
		DispatchGameThread(Params, AsyncCallback);
	}
}

// Blueprint async node implementation
UPerturbationShaderLibrary_AsyncExecution* UPerturbationShaderLibrary_AsyncExecution::ExecutePerturbationShader(
	UObject* WorldContextObject,
	UTextureRenderTarget2D* InOutputRenderTarget,
	FVector2D InCenter)
{
	UPerturbationShaderLibrary_AsyncExecution* Action = NewObject<UPerturbationShaderLibrary_AsyncExecution>();
	Action->OutputRenderTarget = InOutputRenderTarget;
	Action->Center = InCenter;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UPerturbationShaderLibrary_AsyncExecution::Activate()
{
	FPerturbationShaderDispatchParams Params(1, 1, 1);
	Params.Center = Center;
	Params.OutputRenderTarget = OutputRenderTarget;

	FPerturbationShaderInterface::Dispatch(Params, [this]()
	{
		this->Completed.Broadcast();
	});
}
