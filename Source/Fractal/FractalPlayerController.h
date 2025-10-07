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

	// Distance Estimator driven speed control
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	bool bScaleSpeedByDE = true;

	// Target time to reach the fractal surface (approximately)
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	double TimeToSurfaceSeconds = 1.0;

	// Max cap for speed when scaling by DE (cm/s)
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	float MaxSpeed = 3000.0f;

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

	// Debug visualization
	UPROPERTY(EditAnywhere, Category = "Fractal DE|Debug")
	bool bShowFractalDebug = true;

	// Help display toggle
	bool bShowHelp = false;
};
