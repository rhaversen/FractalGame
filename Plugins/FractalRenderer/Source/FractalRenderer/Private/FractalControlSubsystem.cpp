#include "FractalControlSubsystem.h"
#include "FractalRenderer.h"
#include "FractalSceneViewExtension.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogFractalControl, Log, All);

namespace
{
	TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> GetFractalViewExtension()
	{
		if (FFractalRendererModule* Module = FModuleManager::GetModulePtr<FFractalRendererModule>("FractalRenderer"))
		{
			return Module->GetSceneViewExtension();
		}
		return TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe>();
	}
}

void UFractalControlSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogFractalControl, Log, TEXT("FractalControlSubsystem: Initialized"));
	PushParameters();
	PushCameraMapping();
}

void UFractalControlSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UFractalControlSubsystem::SetFractalParameters(const FFractalParameter& InParams)
{
	FractalParameters = InParams;
	PushParameters();
}

void UFractalControlSubsystem::SetEnabled(bool bInEnabled)
{
	FractalParameters.bEnabled = bInEnabled;
	PushParameters();
}

void UFractalControlSubsystem::SetMaxRaySteps(int32 InMaxRaySteps)
{
	FractalParameters.MaxRaySteps = InMaxRaySteps;
	PushParameters();
}

void UFractalControlSubsystem::SetMaxRayDistance(float InMaxRayDistance)
{
	FractalParameters.MaxRayDistance = InMaxRayDistance;
	PushParameters();
}

void UFractalControlSubsystem::SetMaxIterations(int32 InMaxIterations)
{
	FractalParameters.MaxIterations = InMaxIterations;
	PushParameters();
}

void UFractalControlSubsystem::SetBailoutRadius(float InBailoutRadius)
{
	FractalParameters.BailoutRadius = InBailoutRadius;
	PushParameters();
}

void UFractalControlSubsystem::SetMinIterations(int32 InMinIterations)
{
	FractalParameters.MinIterations = InMinIterations;
	PushParameters();
}

void UFractalControlSubsystem::SetConvergenceFactor(float InConvergenceFactor)
{
	FractalParameters.ConvergenceFactor = InConvergenceFactor;
	PushParameters();
}

void UFractalControlSubsystem::SetFractalPower(float InFractalPower)
{
	// The reference manager notices the new power and rebuilds the reference orbit before the next frame.
	FractalParameters.FractalPower = InFractalPower;
	PushParameters();
}

void UFractalControlSubsystem::SetFractalScale(double InScale)
{
	if (InScale > 0.0 && CameraMapping.Scale > 0.0)
	{
		ZoomAroundCamera(InScale / CameraMapping.Scale);
	}
}

void UFractalControlSubsystem::ZoomAroundCamera(double Factor)
{
	if (!(Factor > 0.0) || !FMath::IsFinite(Factor))
	{
		return;
	}
	// Float deltas on the GPU underflow below ~1e-38; keep a margin for pixel offsets and derivatives.
	const double NewScale = CameraMapping.Scale * Factor;
	if (NewScale < 1.0e-30 || NewScale > 1.0e3)
	{
		return;
	}
	CameraMapping.ZoomAround(GetCameraWorldLocation(), Factor);
	PushCameraMapping();
}

void UFractalControlSubsystem::SetCameraFractalPosition(FVector FractalPosition)
{
	CameraMapping.SetFractalPositionOf(GetCameraWorldLocation(), FractalMath::FDDVec3(FractalPosition.X, FractalPosition.Y, FractalPosition.Z));
	PushCameraMapping();
}

FVector UFractalControlSubsystem::GetCameraFractalPosition() const
{
	const FractalMath::FDVec3 P = FractalMath::ToDoubleVec(CameraMapping.WorldToFractal(GetCameraWorldLocation()));
	return FVector(P.X, P.Y, P.Z);
}

double UFractalControlSubsystem::GetCameraDistanceEstimate() const
{
	TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> Extension = GetFractalViewExtension();
	return Extension.IsValid() ? Extension->GetRenderStats().CameraDistanceEstimate : 0.0;
}

void UFractalControlSubsystem::ResetCameraMapping()
{
	CameraMapping = FFractalCameraMapping();
	PushCameraMapping();
}

void UFractalControlSubsystem::SetCameraMapping(const FFractalCameraMapping& InMapping)
{
	CameraMapping = InMapping;
	PushCameraMapping();
}

FVector3d UFractalControlSubsystem::GetCameraWorldLocation() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const APlayerController* Controller = GameInstance->GetFirstLocalPlayerController())
		{
			if (Controller->PlayerCameraManager)
			{
				return Controller->PlayerCameraManager->GetCameraLocation();
			}
		}
	}
	return CameraMapping.Anchor;
}

void UFractalControlSubsystem::PushParameters() const
{
	if (TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> Extension = GetFractalViewExtension())
	{
		Extension->SetFractalParameters(FractalParameters);
	}
}

void UFractalControlSubsystem::PushCameraMapping() const
{
	if (TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> Extension = GetFractalViewExtension())
	{
		Extension->SetCameraMapping(CameraMapping);
	}
}
