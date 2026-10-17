// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FractalPawn.generated.h"

class USphereComponent;
class UCameraComponent;
class UFloatingPawnMovement;

/**
 * Free-flying explorer. AFractalPlayerController rotates it about its own axes (so yaw and pitch respect the
 * current roll) and drives the movement velocity from the distance to the fractal surface.
 */
UCLASS()
class FRACTALGAME_API AFractalPawn : public APawn
{
	GENERATED_BODY()

public:
	AFractalPawn();

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UFloatingPawnMovement> Movement;
};
