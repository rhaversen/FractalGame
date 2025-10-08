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
		static constexpr float AccelPerSpeed = 3.0f;
		static constexpr float MinAccel = 3.0f;
	};

	// Compute target speed from percentage and distance
	// Uses exponential mapping to utilize full percentage range
	float ComputeSpeedFromPercent(float Percent, double DistanceToCM, float BaseMultiplier, float MinSpd, float MaxSpd)
	{
		// Clamp percentage to valid range
		Percent = FMath::Clamp(Percent, 0.0f, 100.0f);
		
		// Exponential scale factor: 0% -> 0.01x, 50% -> 1x, 100% -> 100x
		// This gives fine control at low speeds and allows very fast movement at high percentages
		const float NormalizedPercent = Percent / 100.0f;
		const float ExpFactor = FMath::Pow(10.0f, (NormalizedPercent - 0.5f) * 4.0f); // Range: 0.01 to 100
		
		// Base speed from distance and multiplier
		const float DistanceBasedSpeed = static_cast<float>(DistanceToCM) * BaseMultiplier * ExpFactor;
		
		// Clamp to min/max range
		return FMath::Clamp(DistanceBasedSpeed, MinSpd, MaxSpd);
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
			GInitialSpeedPercent = SpeedPercentage;
			GHaveInitial = true;
		}
	}

	if (!bScaleSpeedByDE)
	{
		return;
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

	const FVector Loc = (PlayerCameraManager) ? PlayerCameraManager->GetCameraLocation() : P->GetActorLocation();
	double Dist = DistanceToFractal(Loc);

	if (!FMath::IsFinite(Dist) || Dist < 0.0)
	{
		return;
	}

	Move->MaxSpeed = ComputeSpeedFromPercent(SpeedPercentage, Dist, BaseSpeedMultiplier, MinSpeed, MaxSpeed);
	Move->Acceleration = FMath::Max(Move->MaxSpeed * SpeedConstraints::AccelPerSpeed, SpeedConstraints::MinAccel);

	Move->Deceleration = 1000000.0f;

	// Update HUD with debug info
	if (AFractalHUD* HUD = Cast<AFractalHUD>(GetHUD()))
	{
		const FVector3d LocalPos = (FVector3d(Loc) - FVector3d(FractalCenter)) / FMath::Max(FractalScale, KINDA_SMALL_NUMBER);
		const float SafeDist = FMath::Max(static_cast<float>(Dist), 0.001f);
		const float LogDist = FMath::LogX(10.0f, SafeDist);
		const float LogMin = FMath::LogX(10.0f, 0.001f);
		const float LogMax = FMath::LogX(10.0f, 1000.0f);
		const float ZoomLevel = FMath::Clamp((1.0f - ((LogDist - LogMin) / (LogMax - LogMin))) * 100.0f, 0.0f, 100.0f);

		HUD->LocalPos = FVector(LocalPos);
		HUD->SpeedPercent = SpeedPercentage;
		HUD->ZoomLevel = ZoomLevel;
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
					GInitialSpeedPercent = SpeedPercentage;
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
