#include "FractalGameMode.h"

#include "FractalPawn.h"
#include "MandelbrotPerturbationSubsystem.h"
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

    // Initialize with a default viewport centered on the origin.
    Subsystem->SetViewportParameters(FVector2D::ZeroVector, 3.0, 1.0, 2048);
}
