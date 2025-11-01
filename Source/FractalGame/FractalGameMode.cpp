#include "FractalGameMode.h"

#include "FractalPawn.h"
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
}
