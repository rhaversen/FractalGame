#include "FractalControlSubsystem.h"
#include "FractalRenderer.h"
#include "FractalSceneViewExtension.h"
#include "Math/UnrealMathUtility.h"

void UFractalControlSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Default values are set by the FFractalParameter constructor
	UE_LOG(LogTemp, Log, TEXT("FractalControlSubsystem: Initialized"));

	// Set initial parameters
	UpdateSceneViewExtension();
}

void UFractalControlSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UFractalControlSubsystem::SetFractalParameters(const FFractalParameter& InParams)
{
	FractalParameters = InParams;
	UpdateSceneViewExtension();
}

void UFractalControlSubsystem::SetEnabled(bool bInEnabled)
{
	if (FractalParameters.bEnabled != bInEnabled)
	{
		FractalParameters.bEnabled = bInEnabled;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetCenter(FVector2D InCenter)
{
	if (!FractalParameters.Center.Equals(InCenter))
	{
		FractalParameters.Center = InCenter;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetZoom(float InZoom)
{
	if (!FMath::IsNearlyEqual(FractalParameters.Zoom, InZoom))
	{
		FractalParameters.Zoom = InZoom;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetMaxRaySteps(int32 InMaxRaySteps)
{
	if (FractalParameters.MaxRaySteps != InMaxRaySteps)
	{
		FractalParameters.MaxRaySteps = InMaxRaySteps;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetMaxRayDistance(float InMaxRayDistance)
{
	if (!FMath::IsNearlyEqual(FractalParameters.MaxRayDistance, InMaxRayDistance))
	{
		FractalParameters.MaxRayDistance = InMaxRayDistance;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetMaxIterations(int32 InMaxIterations)
{
	if (FractalParameters.MaxIterations != InMaxIterations)
	{
		FractalParameters.MaxIterations = InMaxIterations;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetBailoutRadius(float InBailoutRadius)
{
	if (!FMath::IsNearlyEqual(FractalParameters.BailoutRadius, InBailoutRadius))
	{
		FractalParameters.BailoutRadius = InBailoutRadius;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetMinIterations(int32 InMinIterations)
{
	if (FractalParameters.MinIterations != InMinIterations)
	{
		FractalParameters.MinIterations = InMinIterations;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetConvergenceFactor(float InConvergenceFactor)
{
	if (!FMath::IsNearlyEqual(FractalParameters.ConvergenceFactor, InConvergenceFactor))
	{
		FractalParameters.ConvergenceFactor = InConvergenceFactor;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetFractalPower(float InFractalPower)
{
	if (!FMath::IsNearlyEqual(FractalParameters.FractalPower, InFractalPower))
	{
		FractalParameters.FractalPower = InFractalPower;
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::UpdateSceneViewExtension()
{
	// Get the module and its scene view extension
	FFractalRendererModule& Module = FModuleManager::GetModuleChecked<FFractalRendererModule>("FractalRenderer");
	TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> Extension = Module.GetSceneViewExtension();
	
	if (Extension.IsValid())
	{
		Extension->SetFractalParameters(FractalParameters);
	}
}
