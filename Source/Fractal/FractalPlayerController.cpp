// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPlayerController.h"
#include "FractalHUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/Engine.h"

// Persistent initial state for reset
namespace {
	static FTransform GInitialPawnTransform;
	static float GInitialSpeedPercent = 50.0f;
	static bool GHaveInitial = false;

	struct SpeedConstraints
	{
		// Acceleration: applied when input aligns with current velocity direction
		static constexpr float AccelPerSpeed = 0.2f;   // Scales with max speed for consistent feel across zoom levels. Controls the general acceleration.
		static constexpr float MinAccel = 2.0f;        // Minimum to prevent player getting stuck.
		
		// Natural deceleration: applied when no input is given to gradually slow movement
		static constexpr float DecelPerSpeed = 3.0f;   // Scales with speed for gradual braking at any zoom level.
		static constexpr float MinDecel = 0.3f;        // Minimum to ensure eventual stop at very low speeds.

		// Directional braking: applied when input opposes current velocity for responsive direction changes
		static constexpr float BrakePerSpeed = 4.0f;   // Scales with max speed to enable sharp turns at any zoom level.
		static constexpr float MinBrake = 1.0f;        // Minimum ensures direction changes work even when nearly stationary.
	};

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
		
		const float MinTimeToSurface = 0.01f;      // At 100%
		const float MaxTimeToSurface = 1000.0f;    // At ~0% (asymptotic)
		
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
}

AFractalPlayerController::AFractalPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AFractalPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GHaveInitial)
	{
		if (APawn* PInit = GetPawn())
		{
			GInitialPawnTransform = PInit->GetActorTransform();
			GHaveInitial = true;
		}
	}

	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}

	UFloatingPawnMovement* Move = P->FindComponentByClass<UFloatingPawnMovement>();
	if (!Move)
	{
		return;
	}

	if (!bScaleSpeedByDE)
	{
		return;
	}

	const FVector Loc = (PlayerCameraManager) ? PlayerCameraManager->GetCameraLocation() : P->GetActorLocation();
	
	// Get distance to fractal surface from current position
	const double Distance = DistanceToFractal(Loc);
	const float DistanceFloat = static_cast<float>(Distance);
	
	// Calculate max allowed speed based on distance
	const float MaxAllowedSpeed = ComputeSpeedFromPercent(SpeedPercentage, DistanceFloat, 1.0f, MinSpeed, MaxSpeed);
	
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
		const float AccelMagnitude = AccelerationDelta.Size();
		const float MinAccelDelta = SpeedConstraints::MinAccel * DeltaTime;
		const float ClampedMagnitude = FMath::Max(AccelMagnitude, MinAccelDelta);
		const FVector FinalAcceleration = AccelerationDelta.GetSafeNormal() * ClampedMagnitude;
		
		Move->Velocity += FinalAcceleration;
		
		// Apply directional braking: only brake the component of velocity opposing input direction
		if (!Move->Velocity.IsNearlyZero())
		{
			const float CurrentSpeed = Move->Velocity.Size();
			const float ScaledBrake = FMath::Max(SpeedConstraints::BrakePerSpeed * CurrentSpeed, SpeedConstraints::MinBrake);
			
			// Project velocity onto input direction
			const float VelocityAlongInput = FVector::DotProduct(Move->Velocity, NormalizedInput);
			
			// If we have velocity opposing the input (negative projection), brake that component
			if (VelocityAlongInput < 0.0f)
			{
				// Calculate the opposing velocity component vector
				const FVector OpposingComponent = NormalizedInput * VelocityAlongInput;
				
				// Apply braking force only to the opposing component (in the direction of input)
				// Magnitude is proportional to how much we're moving against the input
				const float BrakingMagnitude = FMath::Min(ScaledBrake * DeltaTime, FMath::Abs(VelocityAlongInput));
				const FVector BrakingForce = NormalizedInput * BrakingMagnitude;
				
				Move->Velocity += BrakingForce;
			}
		}
	}
	else
	{
		// Apply deceleration scaled with current speed when no input is given
		const float CurrentSpeed = Move->Velocity.Size();
		const float ScaledDecel = FMath::Max(SpeedConstraints::DecelPerSpeed * CurrentSpeed, SpeedConstraints::MinDecel);
		Move->Deceleration = ScaledDecel;
	}
	
	// Always reset accumulated input every frame to prevent stale input from persisting
	AccumulatedMovementInput = FVector::ZeroVector;
	
	// Clamp velocity to max allowed speed (FloatingPawnMovement doesn't do this automatically)
	const float CurrentSpeed = Move->Velocity.Size();
	if (CurrentSpeed > MaxAllowedSpeed)
	{
		// Scale down to max allowed, preserving direction
		Move->Velocity = Move->Velocity.GetSafeNormal() * MaxAllowedSpeed;
	}

	// Update HUD with debug info
	if (AFractalHUD* HUD = Cast<AFractalHUD>(GetHUD()))
	{
		const FVector3d LocalPos = (FVector3d(Loc) - FVector3d(FractalCenter)) / FMath::Max(FractalScale, KINDA_SMALL_NUMBER);
		const float SafeDist = FMath::Max(DistanceFloat, 0.001f);
		const float LogDist = FMath::LogX(10.0f, SafeDist);
		const float LogMin = FMath::LogX(10.0f, 0.001f);
		const float LogMax = FMath::LogX(10.0f, 1000.0f);
		const float ZoomLevel = FMath::Clamp((1.0f - ((LogDist - LogMin) / (LogMax - LogMin))) * 100.0f, 0.0f, 100.0f);

		HUD->LocalPos = FVector(LocalPos);
		HUD->SpeedPercent = SpeedPercentage;
		HUD->ZoomLevel = ZoomLevel;
		HUD->Distance = DistanceFloat;
		HUD->MaxSpeed = MaxAllowedSpeed;
		HUD->CurrentVelocity = Move->Velocity;
		HUD->bShowDebug = bShowFractalDebug;
		HUD->bShowHelp = bShowHelp;
	}
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
	InputComponent->BindAxis(TEXT("Turn"), this, &AFractalPlayerController::Pan);
	InputComponent->BindAxis(TEXT("LookUp"), this, &AFractalPlayerController::Tilt);

	{
		FInputAxisBinding& Wheel = InputComponent->BindAxis(TEXT("MouseWheel"));
		Wheel.AxisDelegate.GetDelegateForManualSet().BindLambda([this](float Value)
		{
			if (FMath::IsNearlyZero(Value)) return;
			// Adjust speed percentage with mouse wheel (5% per wheel notch)
			SpeedPercentage = FMath::Clamp(SpeedPercentage + Value * 5.0f, 0.0f, 100.0f);
		});
	}

	// R key resets pawn transform and speed percentage
	{
		FInputAxisBinding& Reset = InputComponent->BindAxis(TEXT("ResetCamera"));
		Reset.AxisDelegate.GetDelegateForManualSet().BindLambda([this](float Value)
		{
			if (FMath::IsNearlyZero(Value)) return;
			if (!GHaveInitial)
			{
				if (APawn* PInit = GetPawn())
				{
					GInitialPawnTransform = PInit->GetActorTransform();
					// Don't override GInitialSpeedPercent - use the static initializer value (75.0f)
					GHaveInitial = true;
				}
			}
			if (APawn* P = GetPawn())
			{
				P->SetActorTransform(GInitialPawnTransform);
				if (UFloatingPawnMovement* Move = P->FindComponentByClass<UFloatingPawnMovement>())
				{
					Move->Velocity = FVector::ZeroVector;
				}
			}
			SpeedPercentage = GInitialSpeedPercent;
		});
	}

	// H key shows help while held
	{
		FInputAxisBinding& Help = InputComponent->BindAxis(TEXT("ToggleHelp"));
		Help.AxisDelegate.GetDelegateForManualSet().BindLambda([this](float Value)
		{
			bShowHelp = !FMath::IsNearlyZero(Value);
		});
	}

    // Escape: quit game / close process (now via Action Mapping 'Quit' in DefaultInput.ini)
    InputComponent->BindAction(TEXT("Quit"), IE_Pressed, this, &AFractalPlayerController::HandleQuit);
}

void AFractalPlayerController::HandleQuit()
{
	// Attempt clean quit; works for PIE & packaged. If not available at link-time, fallback to console command.
#if WITH_ENGINE
	if (UWorld* World = GetWorld())
	{
		ConsoleCommand(TEXT("quit"));
		return;
	}
#endif
}


void AFractalPlayerController::MoveForward(float Value)
{
	APawn* P = GetPawn();
	if (!P) return;
	AccumulatedMovementInput += P->GetActorForwardVector() * Value;
}

void AFractalPlayerController::MoveRight(float Value)
{
	APawn* P = GetPawn();
	if (!P) return;
	AccumulatedMovementInput += P->GetActorRightVector() * Value;
}

void AFractalPlayerController::MoveUp(float Value)
{
	APawn* P = GetPawn();
	if (!P) return;
	AccumulatedMovementInput += P->GetActorUpVector() * Value;
}

void AFractalPlayerController::Pan(float Value)
{
	APawn* P = GetPawn();
	if (!P) return;
	// Rotate around the pawn's local up axis so yaw respects current roll
	const FVector Axis = P->GetActorUpVector();
	const float AngleRad = FMath::DegreesToRadians(Value);
	const FQuat Delta = FQuat(Axis, AngleRad);
	const FQuat NewQ = Delta * P->GetActorQuat();
	P->SetActorRotation(NewQ);
}

void AFractalPlayerController::Tilt(float Value)
{
	APawn* P = GetPawn();
	if (!P) return;
	// Rotate around the pawn's local right axis so pitch respects current roll
	const FVector Axis = P->GetActorRightVector();
	const float AngleRad = FMath::DegreesToRadians(-Value);
	const FQuat Delta = FQuat(Axis, AngleRad);
	const FQuat NewQ = Delta * P->GetActorQuat();
	// Clamp pitch to avoid gimbal flip while preserving yaw/roll
	FRotator NewR = NewQ.Rotator();
	NewR.Pitch = FMath::ClampAngle(NewR.Pitch, -89.0f, 89.0f);
	P->SetActorRotation(NewR);
}

void AFractalPlayerController::Roll(float Value)
{
	APawn* P = GetPawn();
	if (!P) return;
	
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	
	// Target roll velocity based on input
	const float TargetRollVelocity = -Value * RollSpeedDegPerSec;
	
	// Apply acceleration or deceleration
	if (FMath::IsNearlyZero(Value))
	{
		// No input - decelerate to zero
		if (CurrentRollVelocity > 0.0f)
		{
			CurrentRollVelocity = FMath::Max(0.0f, CurrentRollVelocity - RollDeceleration * DeltaTime);
		}
		else if (CurrentRollVelocity < 0.0f)
		{
			CurrentRollVelocity = FMath::Min(0.0f, CurrentRollVelocity + RollDeceleration * DeltaTime);
		}
	}
	else
	{
		// Accelerate toward target velocity
		if (CurrentRollVelocity < TargetRollVelocity)
		{
			CurrentRollVelocity = FMath::Min(TargetRollVelocity, CurrentRollVelocity + RollAcceleration * DeltaTime);
		}
		else if (CurrentRollVelocity > TargetRollVelocity)
		{
			CurrentRollVelocity = FMath::Max(TargetRollVelocity, CurrentRollVelocity - RollAcceleration * DeltaTime);
		}
	}
	
	// Apply roll rotation if there's any velocity
	if (!FMath::IsNearlyZero(CurrentRollVelocity))
	{
		const float AngleRad = FMath::DegreesToRadians(CurrentRollVelocity * DeltaTime);
		// Rotate around the pawn's local forward axis so roll behaves as expected
		const FVector Axis = P->GetActorForwardVector();
		const FQuat DeltaQ = FQuat(Axis, AngleRad);
		const FQuat NewQ = DeltaQ * P->GetActorQuat();
		P->SetActorRotation(NewQ);
	}
}

// Mandelbulb distance estimator (DE)
// Returns approximate distance in world units (cm) from WorldPos to the fractal surface.
// This uses a standard DE for the power-n Mandelbulb.
double AFractalPlayerController::DistanceToFractal(const FVector& WorldPos) const
{
	// Transform to fractal-local space and scale to fractal units
	const FVector3d Pworld = FVector3d(WorldPos);
	const FVector3d C = (Pworld - FVector3d(FractalCenter)) / FMath::Max(FractalScale, KINDA_SMALL_NUMBER);

	FVector3d Z = C;
	double dr = 1.0;
	double r = Z.Size();

	const double Bailout = FMath::Max(FractalBailout, 1e-3);

	for (int32 i = 0; i < FractalIterations; ++i)
	{
		r = Z.Size();
		if (r > Bailout)
		{
			break;
		}

		// Convert to polar coordinates
		const double cosArg = FMath::Clamp(Z.Z / FMath::Max(r, 1e-12), -1.0, 1.0);
		double theta = FMath::Acos(cosArg);
		double phi = FMath::Atan2(Z.Y, Z.X);
		
		// Update derivative BEFORE scaling (matches shader order)
		dr = FMath::Pow(r, FractalPower - 1.0) * FractalPower * dr + 1.0;

		// Scale and rotate the point
		const double zr = FMath::Pow(r, FractalPower);
		theta *= FractalPower;
		phi   *= FractalPower;

		// Convert back to cartesian
		const double sinTheta = FMath::Sin(theta);
		Z = FVector3d(
			zr * sinTheta * FMath::Cos(phi),
			zr * sinTheta * FMath::Sin(phi),
			zr * FMath::Cos(theta)
		);

		Z += C;
	}

	// Distance in fractal units
	const double safeR = FMath::Max(r, 1e-12);
	const double DistFractal = 0.5 * FMath::Loge(safeR) * safeR / FMath::Max(dr, 1e-12);
	// Convert back to world units (cm) and ensure strictly positive tiny epsilon
	double DistWorld = FMath::Abs(DistFractal * FractalScale);
	if (!FMath::IsFinite(DistWorld))
	{
		DistWorld = 0.0;
	}
	return DistWorld;
}

// Raymarch along a direction to find distance to fractal surface
float AFractalPlayerController::RaymarchDirection(const FVector& StartPos, const FVector& Direction) const
{
	if (!Direction.IsNormalized())
	{
		return 0.0f;
	}

	FVector CurrentPos = StartPos;
	float TotalDistance = 0.0f;

	for (int32 Step = 0; Step < RaymarchMaxSteps; ++Step)
	{
		const double DE = DistanceToFractal(CurrentPos);
		
		if (!FMath::IsFinite(DE) || DE < 0.0)
		{
			return TotalDistance;
		}

		// Hit surface
		if (DE < RaymarchEpsilon)
		{
			return TotalDistance;
		}

		// Reached max distance
		if (TotalDistance >= RaymarchMaxDistance)
		{
			return RaymarchMaxDistance;
		}

		// Step forward by the distance estimate
		const float StepSize = static_cast<float>(DE);
		CurrentPos += Direction * StepSize;
		TotalDistance += StepSize;
	}

	return TotalDistance;
}

// Compute directional distances and speed limits based on 6-ray raymarching
FDirectionalSpeedData AFractalPlayerController::ComputeDirectionalSpeeds(const FVector& Position, const FRotator& Rotation) const
{
	FDirectionalSpeedData Data;

	// Get local axis directions in world space
	const FVector Forward = Rotation.RotateVector(FVector::ForwardVector);
	const FVector Right = Rotation.RotateVector(FVector::RightVector);
	const FVector Up = Rotation.RotateVector(FVector::UpVector);

	// Raymarch in all 6 cardinal directions
	Data.DistanceForward = RaymarchDirection(Position, Forward);
	Data.DistanceBack = RaymarchDirection(Position, -Forward);
	Data.DistanceRight = RaymarchDirection(Position, Right);
	Data.DistanceLeft = RaymarchDirection(Position, -Right);
	Data.DistanceUp = RaymarchDirection(Position, Up);
	Data.DistanceDown = RaymarchDirection(Position, -Up);

	// Compute speed for each direction
	// "Away" directions get shorter time (faster speed) and higher minimum speed
	// "Toward" directions get longer time (slower speed) and regular minimum speed
	auto ComputeSpeed = [this](float Distance, bool bIsAway) -> float
	{
		const float Multiplier = bIsAway ? AwaySpeedMultiplier : TowardSpeedMultiplier;
		// For "away" directions, use a much higher minimum speed to allow escape from tight spaces
		const float EffectiveMinSpeed = bIsAway ? (MinSpeed * 100.0f) : MinSpeed;
		return ComputeSpeedFromPercent(SpeedPercentage, Distance, Multiplier, EffectiveMinSpeed, MaxSpeed);
	};

	// For now, treat forward/right/up as "toward" and their opposites as "away"
	// This is a simplification - in reality it depends on current velocity direction
	Data.MaxSpeedForward = ComputeSpeed(Data.DistanceForward, false);
	Data.MaxSpeedBack = ComputeSpeed(Data.DistanceBack, true);
	Data.MaxSpeedRight = ComputeSpeed(Data.DistanceRight, false);
	Data.MaxSpeedLeft = ComputeSpeed(Data.DistanceLeft, true);
	Data.MaxSpeedUp = ComputeSpeed(Data.DistanceUp, false);
	Data.MaxSpeedDown = ComputeSpeed(Data.DistanceDown, true);

	return Data;
}

// Apply directional speed constraints to desired movement
FVector AFractalPlayerController::ApplyDirectionalConstraints(const FVector& DesiredMovement, const FDirectionalSpeedData& SpeedData) const
{
	FVector Result = DesiredMovement;

	// Decompose movement into local axes
	APawn* P = GetPawn();
	if (!P)
	{
		return Result;
	}

	const FVector Forward = P->GetActorForwardVector();
	const FVector Right = P->GetActorRightVector();
	const FVector Up = P->GetActorUpVector();

	// Project desired movement onto each axis
	const float ForwardComponent = FVector::DotProduct(DesiredMovement, Forward);
	const float RightComponent = FVector::DotProduct(DesiredMovement, Right);
	const float UpComponent = FVector::DotProduct(DesiredMovement, Up);

	// Constrain each component based on direction
	auto ConstrainComponent = [](float Component, float MaxPositive, float MaxNegative) -> float
	{
		if (Component > 0.0f)
		{
			return FMath::Min(Component, MaxPositive);
		}
		else if (Component < 0.0f)
		{
			return FMath::Max(Component, -MaxNegative);
		}
		return 0.0f;
	};

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	const float ConstrainedForward = ConstrainComponent(ForwardComponent, SpeedData.MaxSpeedForward * DeltaTime, SpeedData.MaxSpeedBack * DeltaTime);
	const float ConstrainedRight = ConstrainComponent(RightComponent, SpeedData.MaxSpeedRight * DeltaTime, SpeedData.MaxSpeedLeft * DeltaTime);
	const float ConstrainedUp = ConstrainComponent(UpComponent, SpeedData.MaxSpeedUp * DeltaTime, SpeedData.MaxSpeedDown * DeltaTime);

	// Reconstruct constrained movement vector
	Result = Forward * ConstrainedForward + Right * ConstrainedRight + Up * ConstrainedUp;

	return Result;
}
