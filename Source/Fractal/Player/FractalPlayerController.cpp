// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPlayerController.h"
#include "FractalPawn.h"
#include "FractalHUD.h"
#include "MandelbulbDE.h"
#include "MandelboxDE.h"
#include "JuliaSetDE.h"
#include "MengerSpongeDE.h"
#include "SierpinskiDE.h"
#include "BurningShipDE.h"
#include "QuaternionDE.h"
#include "KaleidoscopicIFSDE.h"
#include "FractalTracing.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/Engine.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

// Persistent initial state for reset
namespace
{
	static FTransform GInitialPawnTransform;
	static float GInitialSpeedPercent = 50.0f;
	static bool GHaveInitial = false;

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

		static constexpr FFractalParameterPreset GFractalParameterPresets[] = {
		// 	MinP,  MaxP,  DefP,  MinS,     MaxS,    DefS
			{1.0f, 16.0f, 8.0f,  0.0002f,  0.0020f, 0.0010f},	 // Mandelbulb
			{1.5f, 16.0f, 2.0f,  0.0002f,  0.0020f, 0.0010f},	 // Burning Ship
			{1.0f, 16.0f, 4.0f,  0.0002f,  0.0020f, 0.0010f},	 // Julia Set
			{2.0f, 6.0f,  3.0f,  0.0020f,  0.0050f, 0.0030f},	 // Mandelbox
			{2.0f, 4.0f,  2.5f,  0.0002f,  0.0020f, 0.0010f},	 // Inverted Menger
			{1.0f, 25.0f, 5.0f,  0.0002f,  0.0020f, 0.0010f},    // Quaternion
			{1.5f, 5.0f, 2.0f,   0.0002f,  0.0020f, 0.0010f},    // Sierpinski Tetrahedron
			{1.5f, 2.0f, 1.7f,   0.0002f,  0.0020f, 0.0010f} 	 // Kaleidoscopic IFS
		};

		static_assert(UE_ARRAY_COUNT(GFractalParameterPresets) == AFractalPlayerController::MaxFractalType + 1, "Fractal preset table must match fractal types.");

		static const FFractalParameterPreset &GetPresetForType(int32 FractalType)
		{
			const int32 Index = FMath::Clamp(FractalType, 0, static_cast<int32>(UE_ARRAY_COUNT(GFractalParameterPresets)) - 1);
			return GFractalParameterPresets[Index];
		}
}

AFractalPlayerController::AFractalPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;

	FractalParams.Center = FVector::ZeroVector;
	FractalParams.Scale = 1000.0;
	FractalParams.Iterations = 120;
	FractalParams.Power = 8.0;
	FractalParams.Bailout = 4.0;

	RaymarchParams.MaxSteps = 64;
	RaymarchParams.MaxDistance = 500.0f;
	RaymarchParams.Epsilon = 0.01f;
}

AFractalPlayerController::~AFractalPlayerController()
{
}

void AFractalPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UpdateDistanceEstimator();
	ApplyFractalDefaults();

	// If MPC not assigned in Blueprint, try to find it by name
	if (!FractalMaterialCollection)
	{
		FractalMaterialCollection = LoadObject<UMaterialParameterCollection>(
			nullptr, 
			TEXT("/Game/MPC_FractalParameters.MPC_FractalParameters")
		);
	}

	if (FractalMaterialCollection && GetWorld())
	{
		MPCInstance = GetWorld()->GetParameterCollectionInstance(FractalMaterialCollection);
	}

	UpdateMaterialParameters();
}

void AFractalPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GHaveInitial)
	{
		if (APawn *PInit = GetPawn())
		{
			GInitialPawnTransform = PInit->GetActorTransform();
			GHaveInitial = true;
		}
	}

	APawn *P = GetPawn();
	if (!P)
	{
		return;
	}

	UFloatingPawnMovement *Move = P->FindComponentByClass<UFloatingPawnMovement>();
	if (!Move)
	{
		return;
	}

	if (!bScaleSpeedByDE || !DistanceEstimator.IsValid())
	{
		return;
	}

	const FVector Loc = (PlayerCameraManager) ? PlayerCameraManager->GetCameraLocation() : P->GetActorLocation();

	// Update fractal parameters with current power value and scale
	FractalParams.Power = CurrentPower;
	// The shader multiplies camera position by scaleMultiplier (e.g., 0.001).
	// The C++ DE divides position by Scale, so we need: Scale = 1.0 / scaleMultiplier
	FractalParams.Scale = 1.0 / CurrentScaleMultiplier;

	const double Distance = DistanceEstimator->ComputeDistance(Loc, FractalParams);
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
	if (AFractalHUD *HUD = Cast<AFractalHUD>(GetHUD()))
	{
		const FVector3d LocalPos = (FVector3d(Loc) - FVector3d(FractalParams.Center)) / FMath::Max(FractalParams.Scale, KINDA_SMALL_NUMBER);
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
		FInputAxisBinding &Wheel = InputComponent->BindAxis(TEXT("MouseWheel"));
		Wheel.AxisDelegate.GetDelegateForManualSet().BindLambda([this](float Value)
																{
			if (FMath::IsNearlyZero(Value)) return;
			// Adjust speed percentage with mouse wheel (5% per wheel notch)
			SpeedPercentage = FMath::Clamp(SpeedPercentage + Value * 5.0f, 0.0f, 100.0f); });
	}

	// R key resets pawn transform and speed percentage
	{
		FInputAxisBinding &Reset = InputComponent->BindAxis(TEXT("ResetCamera"));
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
			ApplyFractalDefaults();
			UpdateMaterialParameters(); });
	}

	// H key shows help while held
	{
		FInputAxisBinding &Help = InputComponent->BindAxis(TEXT("ToggleHelp"));
		Help.AxisDelegate.GetDelegateForManualSet().BindLambda([this](float Value)
															   { bShowHelp = !FMath::IsNearlyZero(Value); });
	}

	// Escape: quit game / close process (now via Action Mapping 'Quit' in DefaultInput.ini)
	InputComponent->BindAction(TEXT("Quit"), IE_Pressed, this, &AFractalPlayerController::HandleQuit);

	// Fractal parameter controls (from DefaultInput.ini)
	InputComponent->BindAction(TEXT("CycleFractalType"), IE_Pressed, this, &AFractalPlayerController::CycleFractalType);
	
	// Power adjustment - uses axis so it works while held
	{
		FInputAxisBinding &PowerAdjust = InputComponent->BindAxis(TEXT("AdjustPower"));
		PowerAdjust.AxisDelegate.GetDelegateForManualSet().BindLambda([this](float Value)
		{
			const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
			
			// Target velocity based on input
			const float TargetPowerVelocity = Value * PowerAdjustSpeed;
			
			// Apply acceleration or deceleration
			if (FMath::IsNearlyZero(Value))
			{
				// No input - decelerate to zero
				if (CurrentPowerVelocity > 0.0f)
				{
					CurrentPowerVelocity = FMath::Max(0.0f, CurrentPowerVelocity - PowerAdjustDeceleration * DeltaTime);
				}
				else if (CurrentPowerVelocity < 0.0f)
				{
					CurrentPowerVelocity = FMath::Min(0.0f, CurrentPowerVelocity + PowerAdjustDeceleration * DeltaTime);
				}
			}
			else
			{
				// Check if input direction opposes current velocity (directional braking)
				const bool bOpposingDirection = (Value > 0.0f && CurrentPowerVelocity < 0.0f) || 
												(Value < 0.0f && CurrentPowerVelocity > 0.0f);
				
				// Use deceleration rate when changing direction, acceleration rate otherwise
				const float RateToApply = bOpposingDirection ? PowerAdjustDeceleration : PowerAdjustAcceleration;
				
				// Accelerate toward target velocity
				if (CurrentPowerVelocity < TargetPowerVelocity)
				{
					CurrentPowerVelocity = FMath::Min(TargetPowerVelocity, CurrentPowerVelocity + RateToApply * DeltaTime);
				}
				else if (CurrentPowerVelocity > TargetPowerVelocity)
				{
					CurrentPowerVelocity = FMath::Max(TargetPowerVelocity, CurrentPowerVelocity - RateToApply * DeltaTime);
				}
			}
			
			// Apply power adjustment if there's any velocity
			if (!FMath::IsNearlyZero(CurrentPowerVelocity))
			{
				CurrentPower += CurrentPowerVelocity * DeltaTime;
				const FFractalParameterPreset &Preset = GetFractalPreset(CurrentFractalType);
				CurrentPower = FMath::Clamp(CurrentPower, Preset.MinPower, Preset.MaxPower);
				UpdateMaterialParameters();
			}
		});
	}

	// Scale adjustment - uses axis so it works while held
	{
		FInputAxisBinding &ScaleAdjust = InputComponent->BindAxis(TEXT("AdjustScale"));
		ScaleAdjust.AxisDelegate.GetDelegateForManualSet().BindLambda([this](float Value)
		{
			const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
			
			// Target velocity based on input
			const float TargetScaleVelocity = Value * ScaleAdjustSpeed;
			
			// Apply acceleration or deceleration
			if (FMath::IsNearlyZero(Value))
			{
				// No input - decelerate to zero
				if (CurrentScaleVelocity > 0.0f)
				{
					CurrentScaleVelocity = FMath::Max(0.0f, CurrentScaleVelocity - ScaleAdjustDeceleration * DeltaTime);
				}
				else if (CurrentScaleVelocity < 0.0f)
				{
					CurrentScaleVelocity = FMath::Min(0.0f, CurrentScaleVelocity + ScaleAdjustDeceleration * DeltaTime);
				}
			}
			else
			{
				// Check if input direction opposes current velocity (directional braking)
				const bool bOpposingDirection = (Value > 0.0f && CurrentScaleVelocity < 0.0f) || 
												(Value < 0.0f && CurrentScaleVelocity > 0.0f);
				
				// Use deceleration rate when changing direction, acceleration rate otherwise
				const float RateToApply = bOpposingDirection ? ScaleAdjustDeceleration : ScaleAdjustAcceleration;
				
				// Accelerate toward target velocity
				if (CurrentScaleVelocity < TargetScaleVelocity)
				{
					CurrentScaleVelocity = FMath::Min(TargetScaleVelocity, CurrentScaleVelocity + RateToApply * DeltaTime);
				}
				else if (CurrentScaleVelocity > TargetScaleVelocity)
				{
					CurrentScaleVelocity = FMath::Max(TargetScaleVelocity, CurrentScaleVelocity - RateToApply * DeltaTime);
				}
			}
			
			// Apply scale adjustment if there's any velocity
			if (!FMath::IsNearlyZero(CurrentScaleVelocity))
			{
				CurrentScaleMultiplier += CurrentScaleVelocity * DeltaTime;
				const FFractalParameterPreset &Preset = GetFractalPreset(CurrentFractalType);
				CurrentScaleMultiplier = FMath::Clamp(CurrentScaleMultiplier, Preset.MinScale, Preset.MaxScale);
				UpdateMaterialParameters();
			}
		});
	}
}

void AFractalPlayerController::HandleQuit()
{
	// Attempt clean quit; works for PIE & packaged. If not available at link-time, fallback to console command.
#if WITH_ENGINE
	if (UWorld *World = GetWorld())
	{
		ConsoleCommand(TEXT("quit"));
		return;
	}
#endif
}

void AFractalPlayerController::MoveForward(float Value)
{
	APawn *P = GetPawn();
	if (!P)
		return;
	AccumulatedMovementInput += P->GetActorForwardVector() * Value;
}

void AFractalPlayerController::MoveRight(float Value)
{
	APawn *P = GetPawn();
	if (!P)
		return;
	AccumulatedMovementInput += P->GetActorRightVector() * Value;
}

void AFractalPlayerController::MoveUp(float Value)
{
	APawn *P = GetPawn();
	if (!P)
		return;
	AccumulatedMovementInput += P->GetActorUpVector() * Value;
}

void AFractalPlayerController::Pan(float Value)
{
	APawn *P = GetPawn();
	if (!P)
		return;
	// Rotate around the pawn's local up axis so yaw respects current roll
	const FVector Axis = P->GetActorUpVector();
	const float AngleRad = FMath::DegreesToRadians(Value);
	const FQuat Delta = FQuat(Axis, AngleRad);
	const FQuat NewQ = Delta * P->GetActorQuat();
	P->SetActorRotation(NewQ);
}

void AFractalPlayerController::Tilt(float Value)
{
	APawn *P = GetPawn();
	if (!P)
		return;
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
	APawn *P = GetPawn();
	if (!P)
		return;

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

void AFractalPlayerController::CycleFractalType()
{
	CurrentFractalType = (CurrentFractalType + 1) % (MaxFractalType + 1);
	ApplyFractalDefaults();
	UpdateDistanceEstimator();
	UpdateMaterialParameters();
}

void AFractalPlayerController::UpdateDistanceEstimator()
{
	// Create the appropriate distance estimator based on current fractal type
	// Must match the fractal type indices in FractalMaterial.usf
	switch (CurrentFractalType)
	{
		case 0: // FRACTAL_TYPE_MANDELBULB
			DistanceEstimator = MakeUnique<FMandelbulbDE>();
			break;
		case 1: // FRACTAL_TYPE_BURNING_SHIP
			DistanceEstimator = MakeUnique<FBurningShipDE>();
			break;
		case 2: // FRACTAL_TYPE_JULIA_SET
			DistanceEstimator = MakeUnique<FJuliaSetDE>();
			break;
		case 3: // FRACTAL_TYPE_MANDELBOX
			DistanceEstimator = MakeUnique<FMandelboxDE>();
			break;
		case 4: // FRACTAL_TYPE_INVERTED_MENGER
			DistanceEstimator = MakeUnique<FMengerSpongeDE>();
			break;
		case 5: // FRACTAL_TYPE_QUATERNION
			DistanceEstimator = MakeUnique<FQuaternionDE>();
			break;
		case 6: // FRACTAL_TYPE_SIERPINSKI_TETRAHEDRON
			DistanceEstimator = MakeUnique<FSierpinskiDE>();
			break;
		case 7: // FRACTAL_TYPE_KALEIDOSCOPIC_IFS
			DistanceEstimator = MakeUnique<FKaleidoscopicIFSDE>();
			break;
		default:
			// Fallback to Mandelbulb for unknown types
			DistanceEstimator = MakeUnique<FMandelbulbDE>();
			break;
	}
}

void AFractalPlayerController::UpdateMaterialParameters()
{
	ClampFractalParameters();
	const FFractalParameterPreset &Preset = GetFractalPreset(CurrentFractalType);

	if (MPCInstance)
	{
		MPCInstance->SetScalarParameterValue(FName("FractalType"), static_cast<float>(CurrentFractalType));
		MPCInstance->SetScalarParameterValue(FName("Power"), CurrentPower);
		MPCInstance->SetScalarParameterValue(FName("ScaleMultiplier"), CurrentScaleMultiplier);
	}
	
	if (AFractalHUD* FractalHUD = Cast<AFractalHUD>(GetHUD()))
	{
		FractalHUD->CurrentFractalType = CurrentFractalType;
		FractalHUD->CurrentPower = CurrentPower;
		FractalHUD->CurrentScaleMultiplier = CurrentScaleMultiplier;
		FractalHUD->CurrentFractalPreset = Preset;
	}
}

const FFractalParameterPreset &AFractalPlayerController::GetFractalPreset(int32 FractalType) const
{
	return GetPresetForType(FractalType);
}

void AFractalPlayerController::ApplyFractalDefaults()
{
	const FFractalParameterPreset &Preset = GetFractalPreset(CurrentFractalType);
	CurrentPower = Preset.DefaultPower;
	CurrentScaleMultiplier = Preset.DefaultScale;
	CurrentPowerVelocity = 0.0f;
	CurrentScaleVelocity = 0.0f;
	ClampFractalParameters();
}

void AFractalPlayerController::ClampFractalParameters()
{
	const FFractalParameterPreset &Preset = GetFractalPreset(CurrentFractalType);
	CurrentPower = FMath::Clamp(CurrentPower, Preset.MinPower, Preset.MaxPower);
	CurrentScaleMultiplier = FMath::Clamp(CurrentScaleMultiplier, Preset.MinScale, Preset.MaxScale);
}
