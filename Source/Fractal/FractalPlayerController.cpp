// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/Engine.h"

// Persistent initial state for reset
namespace {
	static FTransform GInitialPawnTransform;
	static double GInitialTTS = 0.0;
	static bool GHaveInitial = false;

	struct SpeedConstraints
	{
		static constexpr float AccelPerSpeed = 3.0f;
		static constexpr float MinAccel = 2.0f;
		static constexpr float MinSpeed = 0.05;
	};

	float ComputeTargetSpeed(double DistanceToCM, double TimeToSurfaceSeconds, float MaxSpeed)
	{
		double RawSpeed = DistanceToCM / FMath::Max(TimeToSurfaceSeconds, (double)KINDA_SMALL_NUMBER);
		RawSpeed = FMath::Clamp(RawSpeed, (double)SpeedConstraints::MinSpeed, (double)MaxSpeed);
		return static_cast<float>(RawSpeed);
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
			GInitialTTS = TimeToSurfaceSeconds;
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

	Move->MaxSpeed = ComputeTargetSpeed(Dist, TimeToSurfaceSeconds, MaxSpeed);
	Move->Acceleration = FMath::Max(Move->MaxSpeed * SpeedConstraints::AccelPerSpeed, SpeedConstraints::MinAccel);

	const float SpeedRatio = Move->MaxSpeed / FMath::Max(static_cast<float>(MaxSpeed), KINDA_SMALL_NUMBER);
	const float DecelBoost = 1.f + (1.f - SpeedRatio) * 5.f;
	Move->Deceleration = Move->Acceleration * DecelBoost;

	if (bShowFractalDebug && GEngine)
	{
		const FVector Vel = P->GetVelocity();
		const float VelMag = Vel.Size();
		const FVector3d LocalPos = (FVector3d(Loc) - FVector3d(FractalCenter)) / FMath::Max(FractalScale, KINDA_SMALL_NUMBER);
		
		const float ClampedTTS = FMath::Clamp(TimeToSurfaceSeconds, 0.1, 3.0);
		const float SpeedPercent = FMath::Clamp(100.0f * (1.0f - (ClampedTTS - 0.1f) / 2.9f), 0.0f, 100.0f);
		
		const float SafeDist = FMath::Max(static_cast<float>(Dist), 0.001f);
		const float LogDist = FMath::LogX(10.0f, SafeDist);
		const float LogMin = FMath::LogX(10.0f, 0.001f);
		const float LogMax = FMath::LogX(10.0f, 1000.0f);
		const float ZoomLevel = FMath::Clamp((1.0f - ((LogDist - LogMin) / (LogMax - LogMin))) * 100.0f, 0.0f, 100.0f);
		
		const FColor HeaderColor = FColor(100, 200, 255);
		const FColor BarColor = FColor(0, 255, 150);
		const FColor PosColor = FColor(200, 150, 200);
		
		int32 LineKey = (int32)((PTRINT)this);
		
		auto CreateBar = [](float Percent, int32 Width = 30) -> FString
		{
			int32 Filled = FMath::RoundToInt((Percent / 100.0f) * Width);
			Filled = FMath::Clamp(Filled, 0, Width);
			FString Bar = TEXT("[");
			for (int32 i = 0; i < Width; ++i)
			{
				Bar += (i < Filled) ? TEXT("I") : TEXT(" ");
			}
			Bar += TEXT("]");
			return Bar;
		};
		
		GEngine->AddOnScreenDebugMessage(LineKey++, 0.0f, HeaderColor, 
			TEXT("+--------------------------------------------+"), true, FVector2D(1.3f, 1.3f));
		
		const FString ZoomBar = CreateBar(ZoomLevel, 30);
		GEngine->AddOnScreenDebugMessage(LineKey++, 0.0f, FColor(255, 150, 50), 
			FString::Printf(TEXT("| Zoom  %s %.0f%%"), *ZoomBar, ZoomLevel), 
			true, FVector2D(1.3f, 1.3f));
		
		const FString SpeedBar = CreateBar(SpeedPercent, 30);
		GEngine->AddOnScreenDebugMessage(LineKey++, 0.0f, BarColor, 
			FString::Printf(TEXT("| Speed %s %.0f%%"), *SpeedBar, SpeedPercent), 
			true, FVector2D(1.3f, 1.3f));
		
		GEngine->AddOnScreenDebugMessage(LineKey++, 0.0f, PosColor, 
			FString::Printf(TEXT("| X: %+.4f"), static_cast<float>(LocalPos.X)), 
			true, FVector2D(1.3f, 1.3f));
		GEngine->AddOnScreenDebugMessage(LineKey++, 0.0f, PosColor, 
			FString::Printf(TEXT("| Y: %+.4f"), static_cast<float>(LocalPos.Y)), 
			true, FVector2D(1.3f, 1.3f));
		GEngine->AddOnScreenDebugMessage(LineKey++, 0.0f, PosColor, 
			FString::Printf(TEXT("| Z: %+.4f"), static_cast<float>(LocalPos.Z)), 
			true, FVector2D(1.3f, 1.3f));
		
		GEngine->AddOnScreenDebugMessage(LineKey++, 0.0f, HeaderColor, 
			TEXT("+--------------------------------------------+"), true, FVector2D(1.3f, 1.3f));
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
			TimeToSurfaceSeconds = FMath::Clamp(TimeToSurfaceSeconds - Value * 0.25, 0.1, 10.0);
		});
	}

	// R key resets pawn transform and TimeToSurfaceSeconds
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
					GInitialTTS = TimeToSurfaceSeconds;
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
			TimeToSurfaceSeconds = GInitialTTS;
		});
	}
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
