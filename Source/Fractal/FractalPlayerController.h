// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FractalPlayerController.generated.h"

UCLASS()
class AFractalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFractalPlayerController();

protected:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupInputComponent() override;

private:
	// Basic input-driven movement/rotation

	// Input callbacks
	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void Pan(float Value);
	void Tilt(float Value);
	void Roll(float Value);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Movement", meta = (AllowPrivateAccess = "true"))
	float RollSpeedDegPerSec = 90.0f;
};
