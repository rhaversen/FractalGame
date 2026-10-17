// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FractalGameMode.generated.h"

/** Explorer pawn, flight controller and HUD (set as the project's global default game mode). */
UCLASS()
class FRACTALGAME_API AFractalGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFractalGameMode();
};
