#include "FractalControlSubsystem.h"
#include "FractalRenderer.h"
#include "FractalSceneViewExtension.h"

void UFractalControlSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Default values
	bEnabled = true;
	Center = FVector2D(0.0, 0.0);

	UE_LOG(LogTemp, Log, TEXT("FractalControlSubsystem: Initialized"));

	// Set initial parameters
	UpdateSceneViewExtension();
}

void UFractalControlSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UFractalControlSubsystem::SetEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	UpdateSceneViewExtension();
}

void UFractalControlSubsystem::SetCenter(FVector2D InCenter)
{
	Center = InCenter;
	UpdateSceneViewExtension();
}

void UFractalControlSubsystem::UpdateSceneViewExtension()
{
	// Get the module and its scene view extension
	FFractalRendererModule& Module = FModuleManager::GetModuleChecked<FFractalRendererModule>("FractalRenderer");
	TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> Extension = Module.GetSceneViewExtension();
	
	if (Extension.IsValid())
	{
		Extension->bEnabled = bEnabled;
		Extension->Center = Center;
	}
}
