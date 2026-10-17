// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalGameMode.h"
#include "FractalPawn.h"
#include "FractalPlayerController.h"
#include "UI/FractalHUD.h"

AFractalGameMode::AFractalGameMode()
{
	DefaultPawnClass = AFractalPawn::StaticClass();
	PlayerControllerClass = AFractalPlayerController::StaticClass();
	HUDClass = AFractalHUD::StaticClass();
}
