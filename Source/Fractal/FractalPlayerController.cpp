// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPlayerController.h"
#include "GameFramework/Pawn.h"

AFractalPlayerController::AFractalPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AFractalPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
