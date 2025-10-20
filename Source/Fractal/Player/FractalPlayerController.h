// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FractalTypes.h"
#include "FractalPlayerController.generated.h"

class IFractalDistanceEstimator;

UCLASS()
class AFractalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFractalPlayerController();
	virtual ~AFractalPlayerController();

protected:
	virtual void BeginPlay() override;
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
	void HandleQuit();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Movement", meta = (AllowPrivateAccess = "true"))
	float RollSpeedDegPerSec = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Movement", meta = (AllowPrivateAccess = "true"))
	float RollAcceleration = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Movement", meta = (AllowPrivateAccess = "true"))
	float RollDeceleration = 360.0f;

	// Current roll velocity (degrees per second)
	float CurrentRollVelocity = 0.0f;

	// Accumulated movement input for this frame (processed in Tick)
	FVector AccumulatedMovementInput = FVector::ZeroVector;

	// Distance Estimator driven speed control
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	bool bScaleSpeedByDE = true;

	// Speed control as percentage (0-100%). User controls this directly.
	// 0% = 5 seconds to reach surface, 100% = 1 second to reach surface
	UPROPERTY(EditAnywhere, Category = "Fractal DE", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SpeedPercentage = 50.0f;

	// Minimum speed regardless of distance (cm/s) - prevents getting stuck at tiny scales
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	float MinSpeed = 0.1f;

	// Maximum speed cap (cm/s)
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	float MaxSpeed = 1000.0f;

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Parameters")
	FFractalParameters FractalParams;

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Parameters")
	FRaymarchParameters RaymarchParams;

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Debug")
	bool bShowFractalDebug = true;

	// Help display toggle
	bool bShowHelp = false;

	TUniquePtr<IFractalDistanceEstimator> DistanceEstimator;
};
