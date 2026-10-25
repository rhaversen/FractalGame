// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FractalParameter.h"
#include "FractalFlightRecorder.h"
#include "FractalPlayerController.generated.h"

class UFractalControlSubsystem;

/**
 * Flight controls, fractal selection and zoom.
 *
 * The speed limit follows the distance to the fractal surface, so flying towards it zooms exponentially.
 * Close to the surface the controller keeps world-space distances within a comfortable band by rescaling world
 * units around the camera (UFractalControlSubsystem::ZoomAroundCamera); the perturbation renderer then keeps
 * the image exact far below float precision. Without zoom (far from the surface) everything behaves exactly as
 * in version 1.1.
 */
UCLASS()
class FRACTALGAME_API AFractalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFractalPlayerController();

	// Fractal currently rendered (Tab cycles through all of them)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	EFractalType CurrentFractalType = EFractalType::Mandelbulb;

	// Exponent of the power fractals, scale factor of the folding fractals
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal", meta = (ClampMin = "1.0", ClampMax = "25.0"))
	float CurrentPower = 8.0f;

	// Power adjustment parameters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	float PowerAdjustSpeed = 2.0f; // Max units per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	float PowerAdjustAcceleration = 0.5f; // How fast it ramps up

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	float PowerAdjustDeceleration = 4.0f; // How fast it slows down

	// Fractal units per centimetre before any zoom (UFractalControlSubsystem::SetUserScale): the fractal grows or
	// shrinks around its origin
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal", meta = (ClampMin = "0.00001", ClampMax = "0.01"))
	float CurrentScaleMultiplier = 0.001f;

	// Scale adjustment parameters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	float ScaleAdjustSpeed = 0.001f; // Max units per second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	float ScaleAdjustAcceleration = 0.00005f; // How fast it ramps up

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	float ScaleAdjustDeceleration = 2.0f; // How fast it slows down

	// --- Flight recorder (console commands; M marks, F9 pauses recording) ---

	/** Saves a mark with a note, e.g. FractalMark "holes at the rim". */
	UFUNCTION(Exec)
	void FractalMark(const FString& Note);

	/** Flies back to a mark exactly. Session: a folder name under Saved/FlightLogs; empty = this run, else the latest with marks. */
	UFUNCTION(Exec)
	void FractalGoToMark(int32 Mark, const FString& Session);

	/** Lists the marks of a session (same session rules as FractalGoToMark). */
	UFUNCTION(Exec)
	void FractalListMarks(const FString& Session);

	/** Pauses or resumes the flight log. */
	UFUNCTION(Exec)
	void FractalToggleRecording();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupInputComponent() override;

private:
	// Input callbacks
	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void Pan(float Value);
	void Tilt(float Value);
	void Roll(float Value);
	void AdjustSpeedPercentage(float Value);
	void AdjustPower(float Value);
	void AdjustScale(float Value);
	void ResetCamera(float Value);
	void ShowHelp(float Value);
	void HandleQuit();
	void CycleFractalType();
	void RaiseTargetFrameRate();
	void LowerTargetFrameRate();
	void StepTargetFrameRate(int32 Direction);

	/** Frame rates PageUp / PageDown step through (0 = adaptive resolution off). */
	static constexpr float TargetFrameRates[] = {0.0f, 30.0f, 45.0f, 60.0f, 90.0f, 120.0f, 144.0f};

	/** Start position, speed and the current fractal's defaults, with the fractal origin in front of the start. */
	void ResetToStart();
	void ApplyFractalDefaults();
	void ClampFractalParameters();
	void UpdateFractalParameters();

	/** Keeps the camera distance inside the zoom band; returns the scale factor applied to world units (1 = none). */
	double UpdateZoom(UFractalControlSubsystem& Fractal, double CameraDistance) const;

	UFractalControlSubsystem* GetFractalSubsystem() const;

	/** Current view and render state, as the flight recorder stores it. */
	FFractalFlightState CaptureFlightState(const UFractalControlSubsystem& Fractal, double DistanceCm) const;

	/** Puts the camera at a recorded state (fractal, power, scales, exact position, rotation, speed setting). */
	void ApplyFlightState(const FFractalFlightState& State);

	void MarkFromKey();
	void ShowMessage(const FString& Message, FColor Color = FColor::Yellow) const;

	FFractalFlightRecorder FlightRecorder;

	// Unzoomed camera-to-surface distance of the last tick (cm), for marks taken between ticks
	double LastDistanceCm = 0.0;

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

	// Speed control as percentage (0-100%). User controls this directly with the mouse wheel.
	// 0% = stationary, 100% = 0.01 seconds to reach the surface
	UPROPERTY(EditAnywhere, Category = "Fractal DE", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SpeedPercentage = 50.0f;

	// Minimum speed regardless of distance (cm/s) - prevents getting stuck at tiny scales
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	float MinSpeed = 0.1f;

	// Maximum speed cap (cm/s)
	UPROPERTY(EditAnywhere, Category = "Fractal DE")
	float MaxSpeed = 1000.0f;

	// Navigation and telemetry panels on the HUD
	UPROPERTY(EditAnywhere, Category = "Fractal DE|Debug")
	bool bShowFractalDebug = true;

	// Help display (while H is held)
	bool bShowHelp = false;

	// Speed percentage restored by a reset
	float InitialSpeedPercentage = 50.0f;

	// Pawn transform at the first possession, restored by a reset
	FTransform InitialPawnTransform;
	bool bHaveInitialTransform = false;

	// The camera manager still reports the camera location from before a teleport (reset or start). The subsystem
	// zooms around that location, so zooming waits one frame until it is current again.
	bool bCameraLocationStale = true;
};
