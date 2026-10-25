// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPlayerController.h"
#include "FractalControlSubsystem.h"
#include "UI/FractalHUD.h"
#include "Camera/PlayerCameraManager.h"
#include "Math/RotationMatrix.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/Pawn.h"
#include "Misc/Paths.h"

namespace
{
	struct SpeedConstraints
	{
		// Acceleration: applied when input aligns with current velocity direction
		static constexpr float AccelPerSpeed = 0.2f; // Scales with max speed for consistent feel across zoom levels. Controls the general acceleration.
		static constexpr float MinAccel = 2.0f;		 // Minimum to prevent player getting stuck.

		// Natural deceleration: applied when no input is given to gradually slow movement
		static constexpr float DecelPerSpeed = 3.0f; // Scales with speed for gradual braking at any zoom level.
		static constexpr float MinDecel = 0.3f;		 // Minimum to ensure eventual stop at very low speeds.

		// Directional braking: applied when input opposes current velocity for responsive direction changes
		static constexpr float BrakePerSpeed = 4.0f; // Scales with max speed to enable sharp turns at any zoom level.
		static constexpr float MinBrake = 1.0f;		 // Minimum ensures direction changes work even when nearly stationary.
	};

	// Camera-to-surface distances (world units, cm) kept while zoomed. Version 1.1 rendered in float and ran out of
	// precision a few millimetres from the surface; inside this band its speed and acceleration rules still apply
	// unchanged, and rescaling world units keeps the camera there at any depth.
	struct ZoomBand
	{
		static constexpr double ZoomInBelow = 2.0;
		static constexpr double ZoomOutAbove = 8.0;
		static constexpr double Target = 4.0;
	};

	// Version 1.1's map spawned the player this far from the fractal origin, looking straight at it (cm).
	constexpr double StartDistanceToFractalOrigin = 2350.0;

	// Compute target speed from percentage and distance using logarithmic time-to-surface approach
	// 0% = 0 velocity (stationary)
	// 100% = 0.01 seconds to reach surface (very fast)
	// Uses fourth root for finer control at higher speeds
	float ComputeSpeedFromPercent(float Percent, double DistanceToCM, float TimeMultiplier, float MinSpd, float MaxSpd)
	{
		// Clamp percentage to valid range
		Percent = FMath::Clamp(Percent, 0.0f, 100.0f);

		// At 0%, return 0 velocity
		if (Percent <= 0.0f)
		{
			return 0.0f;
		}

		const float MinTimeToSurface = 0.01f;	// At 100%
		const float MaxTimeToSurface = 1000.0f; // At ~0% (asymptotic)

		const float NormalizedPercent = Percent / 100.0f;
		const float LogMin = FMath::Loge(MinTimeToSurface);
		const float LogMax = FMath::Loge(MaxTimeToSurface);

		// Use fourth root less fidelity at slow speeds and give fine control at high speeds
		const float ExpandedPercent = FMath::Pow(NormalizedPercent, 0.25f);
		const float LogTime = FMath::Lerp(LogMax, LogMin, ExpandedPercent);
		float TimeToSurface = FMath::Exp(LogTime);

		// Apply multiplier: lower multiplier = longer time = slower speed
		TimeToSurface /= FMath::Max(TimeMultiplier, 0.1f);

		// Calculate speed: Distance / Time (cm/s)
		const float CalculatedSpeed = static_cast<float>(DistanceToCM) / FMath::Max(TimeToSurface, 0.01f);

		// Clamp to min/max range to prevent extreme values
		return FMath::Clamp(CalculatedSpeed, MinSpd, MaxSpd);
	}

	// Moves an adjustment velocity towards Input * MaxRate. Without input it decays at StopRate; with input it
	// changes at Rate, or at OpposingRate while the input points against the current velocity.
	void StepAdjustVelocity(float& Velocity, float Input, float MaxRate, float Rate, float OpposingRate, float StopRate, float DeltaTime)
	{
		if (FMath::IsNearlyZero(Input))
		{
			if (Velocity > 0.0f)
			{
				Velocity = FMath::Max(0.0f, Velocity - StopRate * DeltaTime);
			}
			else if (Velocity < 0.0f)
			{
				Velocity = FMath::Min(0.0f, Velocity + StopRate * DeltaTime);
			}
			return;
		}

		const float TargetVelocity = Input * MaxRate;
		const bool bOpposingDirection = (Input > 0.0f && Velocity < 0.0f) || (Input < 0.0f && Velocity > 0.0f);
		const float RateToApply = bOpposingDirection ? OpposingRate : Rate;

		// Accelerate toward target velocity
		if (Velocity < TargetVelocity)
		{
			Velocity = FMath::Min(TargetVelocity, Velocity + RateToApply * DeltaTime);
		}
		else if (Velocity > TargetVelocity)
		{
			Velocity = FMath::Max(TargetVelocity, Velocity - RateToApply * DeltaTime);
		}
	}
}

AFractalPlayerController::AFractalPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AFractalPlayerController::BeginPlay()
{
	Super::BeginPlay();

	InitialSpeedPercentage = SpeedPercentage;
	ResetToStart();
}

void AFractalPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (InPawn && !bHaveInitialTransform)
	{
		InitialPawnTransform = InPawn->GetActorTransform();
		bHaveInitialTransform = true;
		// Normally the pawn is possessed before BeginPlay, which then places the fractal.
		if (HasActorBegunPlay())
		{
			ResetToStart();
		}
	}
}

void AFractalPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}

	UFloatingPawnMovement* Move = P->FindComponentByClass<UFloatingPawnMovement>();
	UFractalControlSubsystem* Fractal = GetFractalSubsystem();
	if (!Move || !Fractal)
	{
		return;
	}

	Fractal->UpdateAdaptiveResolution(DeltaTime);

	// The subsystem zooms around the camera manager's location, so the distance is measured at the same point. After
	// a teleport that location is outdated for a frame; the camera sits at the pawn's origin, so use that instead.
	const FVector Loc = (PlayerCameraManager && !bCameraLocationStale) ? PlayerCameraManager->GetCameraLocation() : P->GetActorLocation();
	const FFractalDistanceInfo DistanceInfo = Fractal->GetDistanceAtWorld(Loc);
	double Distance = DistanceInfo.WorldDistance;

	if (!bCameraLocationStale)
	{
		const double ZoomApplied = UpdateZoom(*Fractal, Distance);
		if (ZoomApplied != 1.0)
		{
			// World units were just rescaled by 1 / ZoomApplied around the camera. Rescaling the velocity and the
			// distance the same way keeps the motion in fractal space continuous, so the zoom stays invisible.
			Move->Velocity /= ZoomApplied;
			Distance /= ZoomApplied;
		}
	}
	bCameraLocationStale = false;

	// Calculate max allowed speed based on distance
	const float MaxAllowedSpeed = ComputeSpeedFromPercent(SpeedPercentage, Distance, 1.0f, MinSpeed, MaxSpeed);

	// Set movement parameters with speed-scaled acceleration/deceleration for smooth direction changes
	const float ScaledAccel = FMath::Max(SpeedConstraints::AccelPerSpeed * MaxAllowedSpeed, SpeedConstraints::MinAccel);

	Move->MaxSpeed = MaxAllowedSpeed;
	Move->Acceleration = ScaledAccel;
	// We apply deceleration manually below based on user input

	const bool bHasInput = !AccumulatedMovementInput.IsNearlyZero();

	if (bHasInput)
	{
		Move->Deceleration = 0.0f; // Disable natural deceleration while actively accelerating

		// Apply accumulated movement input directly to velocity
		const FVector NormalizedInput = AccumulatedMovementInput.GetClampedToMaxSize(1.0f);
		const FVector DesiredAcceleration = NormalizedInput * ScaledAccel;
		const FVector AccelerationDelta = DesiredAcceleration * DeltaTime;

		// Ensure the acceleration being applied has minimum magnitude to prevent floating point precision loss
		const float AccelMagnitude = static_cast<float>(AccelerationDelta.Size());
		const float MinAccelDelta = SpeedConstraints::MinAccel * DeltaTime;
		const float ClampedMagnitude = FMath::Max(AccelMagnitude, MinAccelDelta);
		const FVector FinalAcceleration = AccelerationDelta.GetSafeNormal() * ClampedMagnitude;

		Move->Velocity += FinalAcceleration;

		// Apply directional braking: only brake the component of velocity opposing input direction
		if (!Move->Velocity.IsNearlyZero())
		{
			const float CurrentSpeed = static_cast<float>(Move->Velocity.Size());
			const float ScaledBrake = FMath::Max(SpeedConstraints::BrakePerSpeed * CurrentSpeed, SpeedConstraints::MinBrake);

			// Project velocity onto input direction
			const float VelocityAlongInput = static_cast<float>(FVector::DotProduct(Move->Velocity, NormalizedInput));

			// If we have velocity opposing the input (negative projection), brake that component
			if (VelocityAlongInput < 0.0f)
			{
				// Apply braking force only to the opposing component (in the direction of input)
				// Magnitude is proportional to how much we're moving against the input
				const float BrakingMagnitude = FMath::Min(ScaledBrake * DeltaTime, FMath::Abs(VelocityAlongInput));
				Move->Velocity += NormalizedInput * BrakingMagnitude;
			}
		}
	}
	else
	{
		// Apply deceleration scaled with current speed when no input is given
		const float CurrentSpeed = static_cast<float>(Move->Velocity.Size());
		Move->Deceleration = FMath::Max(SpeedConstraints::DecelPerSpeed * CurrentSpeed, SpeedConstraints::MinDecel);
	}

	// Always reset accumulated input every frame to prevent stale input from persisting
	AccumulatedMovementInput = FVector::ZeroVector;

	// Clamp velocity to max allowed speed (FloatingPawnMovement doesn't do this automatically)
	if (Move->Velocity.Size() > MaxAllowedSpeed)
	{
		// Scale down to max allowed, preserving direction
		Move->Velocity = Move->Velocity.GetSafeNormal() * MaxAllowedSpeed;
	}

	LastDistanceCm = DistanceInfo.FractalDistance / Fractal->GetUserScale();
	FlightRecorder.Tick(DeltaTime, CaptureFlightState(*Fractal, LastDistanceCm));

	// Update HUD. Distances and speeds are converted to the unzoomed scale (version 1.1 units) so that they keep
	// their meaning at any zoom depth.
	if (AFractalHUD* HUD = Cast<AFractalHUD>(GetHUD()))
	{
		HUD->bRecording = FlightRecorder.IsRecording();
		HUD->MarkCount = FlightRecorder.GetMarkCount();
		const double ZoomFactor = Fractal->GetZoomFactor();
		const double EquivalentDistance = DistanceInfo.FractalDistance / Fractal->GetUserScale();

		// Zoom level: logarithm of the distance from 1000 cm (0%) down to the band target at the deepest zoom (100%)
		const double DeepestDistance = ZoomBand::Target * UFractalControlSubsystem::MinFractalScale / Fractal->GetUserScale();
		const double SafeDist = FMath::Max(EquivalentDistance, DeepestDistance);
		const double LogDist = FMath::Loge(SafeDist);
		const double LogMin = FMath::Loge(DeepestDistance);
		const double LogMax = FMath::Loge(1000.0);
		const double ZoomLevel = FMath::Clamp((1.0 - ((LogDist - LogMin) / (LogMax - LogMin))) * 100.0, 0.0, 100.0);

		HUD->ZoomLevel = static_cast<float>(ZoomLevel);
		HUD->Magnification = 1.0 / ZoomFactor;
		HUD->SpeedPercent = SpeedPercentage;
		HUD->Distance = EquivalentDistance;
		HUD->MaxSpeed = MaxAllowedSpeed * ZoomFactor;
		HUD->CurrentVelocity = Move->Velocity * ZoomFactor;
		HUD->FractalPosition = Fractal->GetCameraFractalPositionDD();
		HUD->FractalScale = Fractal->GetFractalScale();
		HUD->UserScale = Fractal->GetUserScale();
		HUD->TargetFrameRate = Fractal->GetTargetFrameRate();
		HUD->ResolutionScale = Fractal->GetResolutionScale();
		HUD->FrameMilliseconds = Fractal->GetMeasuredFrameMilliseconds();
		if (PlayerCameraManager)
		{
			const FRotator CameraRotation = PlayerCameraManager->GetCameraRotation();
			const FRotationMatrix CameraAxes(CameraRotation);
			HUD->ViewForward = CameraAxes.GetUnitAxis(EAxis::X);
			HUD->ViewUp = CameraAxes.GetUnitAxis(EAxis::Z);
			HUD->ViewRotation = CameraRotation;
		}
		HUD->bShowDebug = bShowFractalDebug;
		HUD->bShowHelp = bShowHelp;
		HUD->CurrentFractalType = CurrentFractalType;
		HUD->CurrentPower = CurrentPower;
		HUD->CurrentScaleMultiplier = CurrentScaleMultiplier;
	}
}

double AFractalPlayerController::UpdateZoom(UFractalControlSubsystem& Fractal, double CameraDistance) const
{
	// Inside a power fractal the distance is 0 and there is nothing to zoom towards (press R to get out).
	const bool bTooClose = CameraDistance > 0.0 && CameraDistance < ZoomBand::ZoomInBelow;
	// Zooming out stops at the unzoomed scale (ZoomAroundCamera caps the factor there).
	const bool bTooFar = CameraDistance > ZoomBand::ZoomOutAbove && Fractal.GetZoomFactor() < 1.0 - 1.0e-9;
	if (!bTooClose && !bTooFar)
	{
		return 1.0;
	}
	return Fractal.ZoomAroundCamera(CameraDistance / ZoomBand::Target);
}

void AFractalPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	check(InputComponent);

	InputComponent->BindAxis(TEXT("MoveForward"), this, &AFractalPlayerController::MoveForward);
	InputComponent->BindAxis(TEXT("MoveRight"), this, &AFractalPlayerController::MoveRight);
	InputComponent->BindAxis(TEXT("MoveUp"), this, &AFractalPlayerController::MoveUp);
	InputComponent->BindAxis(TEXT("Pan"), this, &AFractalPlayerController::Pan);
	InputComponent->BindAxis(TEXT("Tilt"), this, &AFractalPlayerController::Tilt);
	InputComponent->BindAxis(TEXT("Roll"), this, &AFractalPlayerController::Roll);
	// Also support default UE axis names so mouse look works without remapping
	InputComponent->BindAxis(TEXT("Turn"), this, &AFractalPlayerController::Pan);
	InputComponent->BindAxis(TEXT("LookUp"), this, &AFractalPlayerController::Tilt);

	// Mouse wheel: speed percentage. R: reset (while held). H: help (while held).
	InputComponent->BindAxis(TEXT("MouseWheel"), this, &AFractalPlayerController::AdjustSpeedPercentage);
	InputComponent->BindAxis(TEXT("ResetCamera"), this, &AFractalPlayerController::ResetCamera);
	InputComponent->BindAxis(TEXT("ToggleHelp"), this, &AFractalPlayerController::ShowHelp);

	// Power and scale use axes so they keep changing while the buttons are held
	InputComponent->BindAxis(TEXT("AdjustPower"), this, &AFractalPlayerController::AdjustPower);
	InputComponent->BindAxis(TEXT("AdjustScale"), this, &AFractalPlayerController::AdjustScale);

	InputComponent->BindAction(TEXT("Quit"), IE_Pressed, this, &AFractalPlayerController::HandleQuit);
	InputComponent->BindAction(TEXT("CycleFractalType"), IE_Pressed, this, &AFractalPlayerController::CycleFractalType);
	InputComponent->BindAction(TEXT("TargetFrameRateUp"), IE_Pressed, this, &AFractalPlayerController::RaiseTargetFrameRate);
	InputComponent->BindAction(TEXT("TargetFrameRateDown"), IE_Pressed, this, &AFractalPlayerController::LowerTargetFrameRate);
	InputComponent->BindAction(TEXT("FlightMark"), IE_Pressed, this, &AFractalPlayerController::MarkFromKey);
	InputComponent->BindAction(TEXT("FlightToggleRecording"), IE_Pressed, this, &AFractalPlayerController::FractalToggleRecording);
}

void AFractalPlayerController::MoveForward(float Value)
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	AccumulatedMovementInput += P->GetActorForwardVector() * Value;
}

void AFractalPlayerController::MoveRight(float Value)
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	AccumulatedMovementInput += P->GetActorRightVector() * Value;
}

void AFractalPlayerController::MoveUp(float Value)
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	AccumulatedMovementInput += P->GetActorUpVector() * Value;
}

void AFractalPlayerController::Pan(float Value)
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	// Rotate around the pawn's local up axis so yaw respects current roll
	const FVector Axis = P->GetActorUpVector();
	const double AngleRad = FMath::DegreesToRadians(static_cast<double>(Value));
	const FQuat Delta(Axis, AngleRad);
	P->SetActorRotation(Delta * P->GetActorQuat());
}

void AFractalPlayerController::Tilt(float Value)
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	// Rotate around the pawn's local right axis so pitch respects current roll
	const FVector Axis = P->GetActorRightVector();
	const double AngleRad = FMath::DegreesToRadians(-static_cast<double>(Value));
	const FQuat Delta(Axis, AngleRad);
	// Clamp pitch to avoid gimbal flip while preserving yaw/roll
	FRotator NewRotation = (Delta * P->GetActorQuat()).Rotator();
	NewRotation.Pitch = FMath::ClampAngle(NewRotation.Pitch, -89.0, 89.0);
	P->SetActorRotation(NewRotation);
}

void AFractalPlayerController::Roll(float Value)
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;

	// Roll accelerates at the same rate in both directions and stops at RollDeceleration without input
	StepAdjustVelocity(CurrentRollVelocity, -Value, RollSpeedDegPerSec, RollAcceleration, RollAcceleration, RollDeceleration, DeltaTime);

	// Apply roll rotation if there's any velocity
	if (!FMath::IsNearlyZero(CurrentRollVelocity))
	{
		// Rotate around the pawn's local forward axis so roll behaves as expected
		const double AngleRad = FMath::DegreesToRadians(static_cast<double>(CurrentRollVelocity * DeltaTime));
		const FQuat Delta(P->GetActorForwardVector(), AngleRad);
		P->SetActorRotation(Delta * P->GetActorQuat());
	}
}

void AFractalPlayerController::AdjustSpeedPercentage(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	// Adjust speed percentage with mouse wheel (5% per wheel notch)
	SpeedPercentage = FMath::Clamp(SpeedPercentage + Value * 5.0f, 0.0f, 100.0f);
}

void AFractalPlayerController::AdjustPower(float Value)
{
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;

	// Use the deceleration rate when changing direction (directional braking), acceleration rate otherwise
	StepAdjustVelocity(CurrentPowerVelocity, Value, PowerAdjustSpeed, PowerAdjustAcceleration, PowerAdjustDeceleration, PowerAdjustDeceleration, DeltaTime);

	// Apply power adjustment if there's any velocity
	if (!FMath::IsNearlyZero(CurrentPowerVelocity))
	{
		CurrentPower += CurrentPowerVelocity * DeltaTime;
		UpdateFractalParameters();
	}
}

void AFractalPlayerController::AdjustScale(float Value)
{
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;

	// Use the deceleration rate when changing direction (directional braking), acceleration rate otherwise
	StepAdjustVelocity(CurrentScaleVelocity, Value, ScaleAdjustSpeed, ScaleAdjustAcceleration, ScaleAdjustDeceleration, ScaleAdjustDeceleration, DeltaTime);

	// Apply scale adjustment if there's any velocity
	if (!FMath::IsNearlyZero(CurrentScaleVelocity))
	{
		CurrentScaleMultiplier += CurrentScaleVelocity * DeltaTime;
		UpdateFractalParameters();
	}
}

void AFractalPlayerController::ResetCamera(float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		ResetToStart();
	}
}

void AFractalPlayerController::ShowHelp(float Value)
{
	bShowHelp = !FMath::IsNearlyZero(Value);
}

void AFractalPlayerController::HandleQuit()
{
	// Works for PIE and packaged builds
	ConsoleCommand(TEXT("quit"));
}

void AFractalPlayerController::StepTargetFrameRate(int32 Direction)
{
	UFractalControlSubsystem* Fractal = GetFractalSubsystem();
	if (!Fractal)
	{
		return;
	}
	const float Current = Fractal->GetTargetFrameRate();
	int32 Index = 0;
	for (int32 I = 0; I < UE_ARRAY_COUNT(TargetFrameRates); ++I)
	{
		if (FMath::Abs(TargetFrameRates[I] - Current) < FMath::Abs(TargetFrameRates[Index] - Current))
		{
			Index = I;
		}
	}
	Index = FMath::Clamp(Index + Direction, 0, static_cast<int32>(UE_ARRAY_COUNT(TargetFrameRates)) - 1);
	Fractal->SetTargetFrameRate(TargetFrameRates[Index]);
}

void AFractalPlayerController::RaiseTargetFrameRate()
{
	StepTargetFrameRate(1);
}

void AFractalPlayerController::LowerTargetFrameRate()
{
	StepTargetFrameRate(-1);
}

void AFractalPlayerController::CycleFractalType()
{
	const int32 TypeCount = static_cast<int32>(EFractalType::Count);
	CurrentFractalType = static_cast<EFractalType>((static_cast<int32>(CurrentFractalType) + 1) % TypeCount);
	ApplyFractalDefaults();
	UpdateFractalParameters();
}

void AFractalPlayerController::ResetToStart()
{
	if (APawn* P = GetPawn())
	{
		if (bHaveInitialTransform)
		{
			P->SetActorTransform(InitialPawnTransform);
		}
		if (UFloatingPawnMovement* Move = P->FindComponentByClass<UFloatingPawnMovement>())
		{
			Move->Velocity = FVector::ZeroVector;
		}
	}
	SpeedPercentage = InitialSpeedPercentage;
	bCameraLocationStale = true;

	// The defaults (including the user scale) first: ResetView replaces the whole mapping at the new user scale.
	ApplyFractalDefaults();
	UpdateFractalParameters();

	UFractalControlSubsystem* Fractal = GetFractalSubsystem();
	if (Fractal && bHaveInitialTransform)
	{
		const FVector StartForward = InitialPawnTransform.GetRotation().GetForwardVector();
		Fractal->ResetView(InitialPawnTransform.GetLocation() + StartForward * StartDistanceToFractalOrigin);
	}
}

void AFractalPlayerController::ApplyFractalDefaults()
{
	const FractalMath::FFractalPreset& Preset = UFractalControlSubsystem::GetPreset(CurrentFractalType);
	CurrentPower = Preset.DefaultPower;
	CurrentScaleMultiplier = Preset.DefaultScale;
	CurrentPowerVelocity = 0.0f;
	CurrentScaleVelocity = 0.0f;
	ClampFractalParameters();
}

void AFractalPlayerController::ClampFractalParameters()
{
	const FractalMath::FFractalPreset& Preset = UFractalControlSubsystem::GetPreset(CurrentFractalType);
	CurrentPower = FMath::Clamp(CurrentPower, Preset.MinPower, Preset.MaxPower);
	CurrentScaleMultiplier = FMath::Clamp(CurrentScaleMultiplier, Preset.MinScale, Preset.MaxScale);
}

void AFractalPlayerController::UpdateFractalParameters()
{
	ClampFractalParameters();

	UFractalControlSubsystem* Fractal = GetFractalSubsystem();
	if (!Fractal)
	{
		return;
	}
	// Type and power in one update, so the renderer never combines a formula with another fractal's power.
	FFractalParameter Params = Fractal->GetFractalParameters();
	Params.FractalType = CurrentFractalType;
	Params.FractalPower = CurrentPower;
	Fractal->SetFractalParameters(Params);
	Fractal->SetUserScale(CurrentScaleMultiplier);
}

UFractalControlSubsystem* AFractalPlayerController::GetFractalSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UFractalControlSubsystem>() : nullptr;
}

FFractalFlightState AFractalPlayerController::CaptureFlightState(const UFractalControlSubsystem& Fractal, double DistanceCm) const
{
	FFractalFlightState State;
	State.FractalType = CurrentFractalType;
	State.Power = CurrentPower;
	State.UserScale = Fractal.GetUserScale();
	State.Scale = Fractal.GetFractalScale();
	State.Position = Fractal.GetCameraFractalPositionDD();
	State.SpeedPercent = SpeedPercentage;
	State.DistanceCm = DistanceCm;
	State.ResolutionScale = Fractal.GetResolutionScale();
	State.TargetFrameRate = Fractal.GetTargetFrameRate();
	State.Render = Fractal.GetRenderStats();
	if (const APawn* P = GetPawn())
	{
		State.Rotation = P->GetActorQuat();
		if (const UFloatingPawnMovement* Move = P->FindComponentByClass<UFloatingPawnMovement>())
		{
			State.SpeedCmPerSecond = Move->Velocity.Size() * Fractal.GetZoomFactor();
		}
	}
	return State;
}

void AFractalPlayerController::ApplyFlightState(const FFractalFlightState& State)
{
	APawn* P = GetPawn();
	UFractalControlSubsystem* Fractal = GetFractalSubsystem();
	if (!P || !Fractal)
	{
		return;
	}

	CurrentFractalType = State.FractalType;
	CurrentPower = State.Power;
	CurrentScaleMultiplier = static_cast<float>(State.UserScale);
	CurrentPowerVelocity = 0.0f;
	CurrentScaleVelocity = 0.0f;
	CurrentRollVelocity = 0.0f;
	SpeedPercentage = State.SpeedPercent;
	UpdateFractalParameters();

	P->SetActorRotation(State.Rotation);
	if (UFloatingPawnMovement* Move = P->FindComponentByClass<UFloatingPawnMovement>())
	{
		Move->Velocity = FVector::ZeroVector;
	}

	// The camera sits at the pawn's origin; the camera manager reports the new placement only next frame.
	Fractal->PlaceCamera(P->GetActorLocation(), State.Position, State.UserScale, State.Scale);
	bCameraLocationStale = true;
}

void AFractalPlayerController::ShowMessage(const FString& Message, FColor Color) const
{
	UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, Color, Message);
	}
}

void AFractalPlayerController::MarkFromKey()
{
	FractalMark(FString());
}

void AFractalPlayerController::FractalMark(const FString& Note)
{
	const UFractalControlSubsystem* Fractal = GetFractalSubsystem();
	if (!Fractal)
	{
		return;
	}
	const int32 Mark = FlightRecorder.AddMark(CaptureFlightState(*Fractal, LastDistanceCm), Note);
	if (Mark > 0)
	{
		ShowMessage(FString::Printf(TEXT("Mark %d saved%s%s"), Mark, Note.IsEmpty() ? TEXT("") : TEXT(": "), *Note), FColor::Green);
	}
	else
	{
		ShowMessage(TEXT("Could not save the mark (see the log)"), FColor::Red);
	}
}

void AFractalPlayerController::FractalGoToMark(int32 Mark, const FString& Session)
{
	FFractalFlightState State;
	FString Note;
	FString Source;
	if (!FFractalFlightRecorder::LoadMark(Mark, Session, FlightRecorder.GetSessionDirectory(), State, Note, Source))
	{
		ShowMessage(FString::Printf(TEXT("Mark %d not found in %s"), Mark, *Source), FColor::Red);
		return;
	}
	ApplyFlightState(State);
	ShowMessage(FString::Printf(TEXT("At mark %d from %s%s%s"), Mark, *FPaths::GetPath(Source), Note.IsEmpty() ? TEXT("") : TEXT(": "), *Note), FColor::Green);
}

void AFractalPlayerController::FractalListMarks(const FString& Session)
{
	FString Source;
	const TArray<FString> Lines = FFractalFlightRecorder::ListMarks(Session, FlightRecorder.GetSessionDirectory(), Source);
	if (Lines.Num() == 0)
	{
		ShowMessage(FString::Printf(TEXT("No marks in %s"), *Source), FColor::Red);
		return;
	}
	// On screen the newest message is on top, so add them in reverse to read top-down.
	for (int32 I = Lines.Num() - 1; I >= 0; --I)
	{
		ShowMessage(Lines[I], FColor::Cyan);
	}
	ShowMessage(FString::Printf(TEXT("Marks in %s:"), *FPaths::GetPath(Source)), FColor::White);
}

void AFractalPlayerController::FractalToggleRecording()
{
	FlightRecorder.SetRecording(!FlightRecorder.IsRecording());
	ShowMessage(FlightRecorder.IsRecording() ? TEXT("Flight recording on") : TEXT("Flight recording paused"));
}
