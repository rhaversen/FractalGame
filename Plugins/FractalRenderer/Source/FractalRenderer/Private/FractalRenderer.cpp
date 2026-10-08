#include "FractalRenderer.h"
#include "FractalSceneViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Misc/CoreDelegates.h"
#include "Engine/Engine.h"
#include "FractalMath/DoubleDouble.h"

#define LOCTEXT_NAMESPACE "FFractalRendererModule"

void FFractalRendererModule::StartupModule()
{
	// Double-double arithmetic relies on exact IEEE rounding; value-changing optimisations (fast-math,
	// re-association) silently break it and with it every zoom deeper than ~1e-13.
	if (!FractalMath::DDSelfTest())
	{
		UE_LOG(LogTemp, Error, TEXT("FractalRenderer: double-double self test FAILED - the module was compiled with value-changing floating point optimisations. Deep zoom will be wrong."));
	}

	// Map the plugin's shader directory
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("FractalRenderer"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/FractalRendererShaders"), PluginShaderDir);

	// Defer scene view extension registration until the engine is ready
	if (GEngine && GEngine->ViewExtensions)
	{
		RegisterSceneViewExtension();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FFractalRendererModule::RegisterSceneViewExtension);
	}
}

void FFractalRendererModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle = FDelegateHandle();
	}

	// Unregister scene view extension
	SceneViewExtension.Reset();
}

void FFractalRendererModule::RegisterSceneViewExtension()
{
	if (!SceneViewExtension.IsValid())
	{
		SceneViewExtension = FSceneViewExtensions::NewExtension<FFractalSceneViewExtension>();
		UE_LOG(LogTemp, Log, TEXT("FractalRenderer: Scene View Extension registered."));
	}

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle = FDelegateHandle();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFractalRendererModule, FractalRenderer)
