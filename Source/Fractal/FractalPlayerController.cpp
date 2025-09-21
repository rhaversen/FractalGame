// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/Engine.h"

AFractalPlayerController::AFractalPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AFractalPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bScaleSpeedByDE)
	{
		return;
	}

	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}

	// Try to find a FloatingPawnMovement to control speed
	UFloatingPawnMovement* Move = P->FindComponentByClass<UFloatingPawnMovement>();
	if (!Move)
	{
		return;
	}

	// Compute distance to fractal surface from camera location (matches shader logic)
	const FVector Loc = (PlayerCameraManager) ? PlayerCameraManager->GetCameraLocation() : P->GetActorLocation();
	double Dist = DistanceToFractal(Loc); // in cm

	// Guard against invalid/degenerate values
	if (!FMath::IsFinite(Dist) || Dist < 0.0)
	{
		return;
	}

	// Target speed so time to reach surface ~ TimeToSurfaceSeconds
	double TargetSpeed = TimeToSurfaceSeconds > 0.0 ? Dist / TimeToSurfaceSeconds : Dist; // cm/s

	// Clamp only by MaxSpeed cap; allow arbitrarily small speeds if DE is tiny
	TargetSpeed = FMath::Min(static_cast<float>(TargetSpeed), MaxSpeed);

	Move->MaxSpeed = static_cast<float>(TargetSpeed);
	// Keep acceleration/deceleration proportional to maintain feel
	Move->Acceleration = Move->MaxSpeed * 3.0f;
	Move->Deceleration = Move->Acceleration;

	if (bShowFractalDebug && GEngine)
	{
		const FVector Vel = P->GetVelocity();
		const float VelMag = Vel.Size();
		const FVector3d LocalPos = (FVector3d(Loc) - FVector3d(FractalCenter)) / FMath::Max(FractalScale, KINDA_SMALL_NUMBER);
		const FString Msg = FString::Printf(
			TEXT("Fractal: Dist=%.3f cm  Target=%.1f cm/s  MaxSpeed=%.1f  Vel=%.1f cm/s  LocalPos=(%.4f,%.4f,%.4f)"),
			static_cast<float>(Dist), static_cast<float>(TargetSpeed), Move->MaxSpeed, VelMag,
			static_cast<float>(LocalPos.X), static_cast<float>(LocalPos.Y), static_cast<float>(LocalPos.Z));
		GEngine->AddOnScreenDebugMessage((uint64)((PTRINT)this), 0.0f, FColor::Cyan, Msg, true, FVector2D(1.0f, 1.0f));
	}

	// Velocity will be clamped by UFloatingPawnMovement to MaxSpeed during its update.
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
}


void AFractalPlayerController::MoveForward(float Value)
{
	if (Value == 0.0f) return;
	APawn* P = GetPawn();
	if (!P) return;
	P->AddMovementInput(P->GetActorForwardVector(), Value);
}

void AFractalPlayerController::MoveRight(float Value)
{
	if (Value == 0.0f) return;
	APawn* P = GetPawn();
	if (!P) return;
	P->AddMovementInput(P->GetActorRightVector(), Value);
}

void AFractalPlayerController::MoveUp(float Value)
{
	if (Value == 0.0f) return;
	APawn* P = GetPawn();
	if (!P) return;
	// Move along the pawn's local up so roll affects vertical movement
	P->AddMovementInput(P->GetActorUpVector(), Value);
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
	if (Value == 0.0f) return;
	APawn* P = GetPawn();
	if (!P) return;
	const float Delta = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	const float AngleRad = FMath::DegreesToRadians(-Value * RollSpeedDegPerSec * Delta);
	// Rotate around the pawn's local forward axis so roll behaves as expected
	const FVector Axis = P->GetActorForwardVector();
	const FQuat DeltaQ = FQuat(Axis, AngleRad);
	const FQuat NewQ = DeltaQ * P->GetActorQuat();
	P->SetActorRotation(NewQ);
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

		// Scale and rotate the point
		const double zr = FMath::Pow(r, FractalPower);
		// Update derivative
		dr = zr / FMath::Max(r, 1e-12) * FractalPower * dr + 1.0;

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
