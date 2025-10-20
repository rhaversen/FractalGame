// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FractalPawn.generated.h"

class USphereComponent;
class UCameraComponent;
class UFloatingPawnMovement;

UCLASS()
class AFractalPawn : public APawn
{
	GENERATED_BODY()

public:
	AFractalPawn();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	USphereComponent *Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UCameraComponent *Camera;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	UFloatingPawnMovement *Movement;
};
