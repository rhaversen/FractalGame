#include "FractalControlSubsystem.h"
#include "FractalRenderer.h"
#include "FractalSceneViewExtension.h"
#include "MandelbulbOrbitGenerator.h"
#include "Math/UnrealMathUtility.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogFractalControl, Log, All);

void UFractalControlSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Create orbit generator
	OrbitGenerator = MakeUnique<FMandelbulbOrbitGenerator>();

	// Default values are set by the FFractalParameter constructor
	UE_LOG(LogFractalControl, Log, TEXT("FractalControlSubsystem: Initialized"));

	// Generate initial reference orbit
	GenerateReferenceOrbit();

	// Set initial parameters
	UpdateSceneViewExtension();
}

void UFractalControlSubsystem::Deinitialize()
{
	OrbitGenerator.Reset();
	Super::Deinitialize();
}

void UFractalControlSubsystem::SetFractalParameters(const FFractalParameter& InParams)
{
	FractalParameters = InParams;
	
	// Check if orbit needs regeneration
	if (ShouldRegenerateOrbit(InParams))
	{
		GenerateReferenceOrbit();
	}
	
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
		
		// Center change may require orbit regeneration
		if (ShouldRegenerateOrbit(FractalParameters))
		{
			GenerateReferenceOrbit();
		}
		
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
		
		// Iteration count change requires orbit regeneration
		GenerateReferenceOrbit();
		
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::SetBailoutRadius(float InBailoutRadius)
{
	if (!FMath::IsNearlyEqual(FractalParameters.BailoutRadius, InBailoutRadius))
	{
		FractalParameters.BailoutRadius = InBailoutRadius;
		
		// Bailout change may affect orbit
		GenerateReferenceOrbit();
		
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
		
		// Power change requires orbit regeneration
		GenerateReferenceOrbit();
		
		UpdateSceneViewExtension();
	}
}

void UFractalControlSubsystem::RegenerateOrbit()
{
	GenerateReferenceOrbit();
	UpdateSceneViewExtension();
}

bool UFractalControlSubsystem::ShouldRegenerateOrbit(const FFractalParameter& NewParams) const
{
	// Regenerate if critical parameters changed
	const double CenterThreshold = 0.01; // Relative to zoom
	
	// Check center movement (relative to current zoom level)
	FVector2D CenterDelta = NewParams.Center - LastOrbitParams.Center;
	double CenterDistance = CenterDelta.Length();
	double RelativeCenterChange = CenterDistance / FMath::Max(NewParams.Zoom, 1e-10);
	
	if (RelativeCenterChange > CenterThreshold)
	{
		return true;
	}
	
	// Check if iterations changed
	if (NewParams.MaxIterations != LastOrbitParams.MaxIterations)
	{
		return true;
	}
	
	// Check if power changed
	if (!FMath::IsNearlyEqual(NewParams.FractalPower, LastOrbitParams.FractalPower, 0.001f))
	{
		return true;
	}
	
	// Check if bailout changed
	if (!FMath::IsNearlyEqual(NewParams.BailoutRadius, LastOrbitParams.BailoutRadius, 0.001f))
	{
		return true;
	}
	
	return false;
}

void UFractalControlSubsystem::GenerateReferenceOrbit()
{
	if (!OrbitGenerator.IsValid())
	{
		UE_LOG(LogFractalControl, Warning, TEXT("Orbit generator not initialized"));
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE(UFractalControlSubsystem::GenerateReferenceOrbit);
	
	// Reference center in fractal space (Center is 2D, we use Z=0 for 3D Mandelbulb)
	FVector3d ReferenceCenter(FractalParameters.Center.X, FractalParameters.Center.Y, 0.0);
	
	// Generate orbit in double precision
	CurrentOrbit = OrbitGenerator->GenerateOrbit(
		ReferenceCenter,
		static_cast<double>(FractalParameters.FractalPower),
		FractalParameters.MaxIterations,
		static_cast<double>(FractalParameters.BailoutRadius)
	);
	
	// Store parameters used for this orbit
	LastOrbitParams = FractalParameters;
	
	UE_LOG(LogFractalControl, Log, 
		TEXT("Generated reference orbit: Center=(%.6f, %.6f, 0.0), Power=%.2f, Iterations=%d, Valid=%s"),
		ReferenceCenter.X, ReferenceCenter.Y,
		FractalParameters.FractalPower,
		CurrentOrbit.GetLength(),
		CurrentOrbit.IsValid() ? TEXT("Yes") : TEXT("No")
	);

	if (GEngine)
	{
		FString OrbitMessage = FString::Printf(TEXT("Fractal orbit regenerated (%d points)"), CurrentOrbit.GetLength());
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, OrbitMessage);
	}
	
	// Push orbit to view extension
	FFractalRendererModule& Module = FModuleManager::GetModuleChecked<FFractalRendererModule>("FractalRenderer");
	TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> Extension = Module.GetSceneViewExtension();
	
	if (Extension.IsValid())
	{
		Extension->SetReferenceOrbit(CurrentOrbit);
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
