// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FractalTypes.h"
#include "FractalPlayerController.generated.h"

class IFractalDistanceEstimator;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;

UCLASS()
class AFractalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFractalPlayerController();
	virtual ~AFractalPlayerController();

	// Maximum fractal type index (must match FRACTAL_TYPE_COUNT - 1 in shader)
	// When adding fractals: Update shader defines, HUD names array, and this constant
	static constexpr int32 MaxFractalType = 7;

	// Material Parameter Collection for shader control
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material")
	TObjectPtr<UMaterialParameterCollection> FractalMaterialCollection;

	// Current fractal type (0-MaxFractalType based on shader defines)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material", meta = (ClampMin = "0", ClampMax = "7"))
	int32 CurrentFractalType = 0;

	// Current power value for the fractal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material", meta = (ClampMin = "1.0", ClampMax = "19.0"))
	float CurrentPower = 8.0f;

	// Power adjustment parameters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material")
	float PowerAdjustSpeed = 2.0f; // Max units per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material")
	float PowerAdjustAcceleration = 0.5f; // How fast it ramps up

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material")
	float PowerAdjustDeceleration = 4.0f; // How fast it slows down

	// Current scale multiplier for the fractal (multiplies the base scale from GetScaleFromPower)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material", meta = (ClampMin = "0.00001", ClampMax = "0.01"))
	float CurrentScaleMultiplier = 0.001f;

	// Scale adjustment parameters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material")
	float ScaleAdjustSpeed = 0.001f; // Max units per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material")
	float ScaleAdjustAcceleration = 0.00005f; // How fast it ramps up

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Material")
	float ScaleAdjustDeceleration = 2.0f; // How fast it slows down

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
	void CycleFractalType();
	void UpdateDistanceEstimator();
	void UpdateMaterialParameters();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Movement", meta = (AllowPrivateAccess = "true"))
	float RollSpeedDegPerSec = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Movement", meta = (AllowPrivateAccess = "true"))
	float RollAcceleration = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal Movement", meta = (AllowPrivateAccess = "true"))
	float RollDeceleration = 360.0f;

	// Current roll velocity (degrees per second)
	float CurrentRollVelocity = 0.0f;

	// Current power adjustment velocity
	float CurrentPowerVelocity = 0.0f;

	// Current scale adjustment velocity
	float CurrentScaleVelocity = 0.0f;

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

	// Cached MPC instance
	UMaterialParameterCollectionInstance* MPCInstance = nullptr;
};
