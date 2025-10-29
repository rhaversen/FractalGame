#include "FractalGameMode.h"

#include "FractalPawn.h"
#include "MandelbrotPerturbationSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/FractalHUD.h"

AFractalGameMode::AFractalGameMode()
{
    DefaultPawnClass = AFractalPawn::StaticClass();
    HUDClass = AFractalHUD::StaticClass();
}

void AFractalGameMode::BeginPlay()
{
	Super::BeginPlay();

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UMandelbrotPerturbationSubsystem* Subsystem = GameInstance->GetSubsystem<UMandelbrotPerturbationSubsystem>();
	if (!Subsystem)
	{
		return;
	}

	// Initialize with a default viewport centered on the origin for 3D Mandelbulb
	Subsystem->SetViewportParameters(FVector::ZeroVector, 8.0, 2048);
}void AFractalGameMode::SetPerturbationMaterial(UMaterialInstanceDynamic* MaterialInstance)
{
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Warning, TEXT("SetPerturbationMaterial: No GameInstance available"));
        return;
    }

    UMandelbrotPerturbationSubsystem* Subsystem = GameInstance->GetSubsystem<UMandelbrotPerturbationSubsystem>();
    if (!Subsystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("SetPerturbationMaterial: No MandelbrotPerturbationSubsystem available"));
        return;
    }

    if (!MaterialInstance)
    {
        UE_LOG(LogTemp, Warning, TEXT("SetPerturbationMaterial: MaterialInstance is null"));
        return;
    }

    Subsystem->SetTargetMaterial(MaterialInstance);
    Subsystem->ForceRebuild();
    UE_LOG(LogTemp, Log, TEXT("SetPerturbationMaterial: Successfully connected material to subsystem"));
}
