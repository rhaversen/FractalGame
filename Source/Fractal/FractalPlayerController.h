// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FractalPlayerController.generated.h"

// Stores distance and speed limits for each cardinal direction
USTRUCT(BlueprintType)
struct FDirectionalSpeedData
{
	GENERATED_BODY()

	// Distance to fractal surface in each direction (cm)
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float DistanceForward = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float DistanceBack = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float DistanceRight = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float DistanceLeft = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float DistanceUp = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float DistanceDown = 0.0f;

	// Max speed allowed in each direction (cm/s)
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float MaxSpeedForward = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float MaxSpeedBack = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float MaxSpeedRight = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float MaxSpeedLeft = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float MaxSpeedUp = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	float MaxSpeedDown = 0.0f;
};

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

	// Enable directional raymarching for fine-grained speed control
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	bool bUseDirectionalRaymarching = true;

	// Distance threshold below which directional raymarching activates (cm)
	UPROPERTY(EditAnywhere, Category = "Fractal DE", meta = (ClampMin = "0.1"))
	float DirectionalRaymarchThreshold = 100.0f;

	// Raymarching parameters
	UPROPERTY(EditAnywhere, Category = "Fractal DE|Raymarching")
	int32 RaymarchMaxSteps = 64;

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Raymarching")
	float RaymarchMaxDistance = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Raymarching")
	float RaymarchEpsilon = 0.01f;

	// Speed scaling: faster when moving away from surface, slower when moving toward
	UPROPERTY(EditAnywhere, Category = "Fractal DE|Directional Speed")
	float AwaySpeedMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Directional Speed")
	float TowardSpeedMultiplier = 0.5f;

	// Expose directional data for HUD
	UFUNCTION(BlueprintCallable, Category = "Fractal DE")
	FDirectionalSpeedData GetDirectionalSpeedData() const { return CachedSpeedData; }

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

	// Fractal placement and parameters
	UPROPERTY(EditAnywhere, Category = "Fractal DE|Transform")
	FVector FractalCenter = FVector::ZeroVector;

	// World units per fractal unit. Use to match your material/shader scale.
	UPROPERTY(EditAnywhere, Category = "Fractal DE|Transform")
	double FractalScale = 1000.0; // Match material: pos = World * 0.001 => 1 fractal unit = 1000 cm

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Mandelbulb")
	int32 FractalIterations = 120; // Match material default

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Mandelbulb")
	double FractalPower = 8.0;

	UPROPERTY(EditAnywhere, Category = "Fractal DE|Mandelbulb")
	double FractalBailout = 4.0; // Match material default

	// Distance estimator for Mandelbulb fractal centered at FractalCenter with FractalScale
	double DistanceToFractal(const FVector& WorldPos) const;

	// Raymarch along a direction and return distance to surface
	float RaymarchDirection(const FVector& StartPos, const FVector& Direction) const;

	// Compute directional distances and speed limits
	FDirectionalSpeedData ComputeDirectionalSpeeds(const FVector& Position, const FRotator& Rotation) const;

	// Apply directional speed constraints to movement input
	FVector ApplyDirectionalConstraints(const FVector& DesiredMovement, const FDirectionalSpeedData& SpeedData) const;

	// Cached directional speed data
	FDirectionalSpeedData CachedSpeedData;

	// Debug visualization
	UPROPERTY(EditAnywhere, Category = "Fractal DE|Debug")
	bool bShowFractalDebug = true;

	// Help display toggle
	bool bShowHelp = false;

private:
	void HandleQuit();
};
