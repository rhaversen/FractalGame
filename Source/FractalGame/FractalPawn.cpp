// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/FloatingPawnMovement.h"

AFractalPawn::AFractalPawn()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create camera component
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    RootComponent = Camera;

    // Create movement component
    MovementComponent = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("MovementComponent"));
    MovementComponent->MaxSpeed = MovementSpeed;
    MovementComponent->Acceleration = 2000.0f;
    MovementComponent->Deceleration = 4000.0f;

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;
}

void AFractalPawn::BeginPlay()
{
    Super::BeginPlay();
}

void AFractalPawn::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // Bind axis mappings
    PlayerInputComponent->BindAxis("MoveForward", this, &AFractalPawn::MoveForward);
    PlayerInputComponent->BindAxis("MoveRight", this, &AFractalPawn::MoveRight);
    PlayerInputComponent->BindAxis("MoveUp", this, &AFractalPawn::MoveUp);
    PlayerInputComponent->BindAxis("Turn", this, &AFractalPawn::Turn);
    PlayerInputComponent->BindAxis("LookUp", this, &AFractalPawn::LookUp);
    PlayerInputComponent->BindAxis("Roll", this, &AFractalPawn::Roll);
}

void AFractalPawn::MoveForward(float Value)
{
    if (FMath::IsNearlyZero(Value))
    {
        return;
    }

    AddMovementInput(GetActorForwardVector(), Value);
}

void AFractalPawn::MoveRight(float Value)
{
    if (FMath::IsNearlyZero(Value))
    {
        return;
    }

    AddMovementInput(GetActorRightVector(), Value);
}

void AFractalPawn::MoveUp(float Value)
{
    if (FMath::IsNearlyZero(Value))
    {
        return;
    }

    AddMovementInput(GetActorUpVector(), Value);
}

void AFractalPawn::Turn(float Value)
{
    if (FMath::IsNearlyZero(Value))
    {
        return;
    }

    const float AngleRad = FMath::DegreesToRadians(Value * LookSensitivity);
    const FQuat Delta = FQuat(GetActorUpVector(), AngleRad);
    const FQuat NewRotation = Delta * GetActorQuat();
    SetActorRotation(NewRotation);

}

void AFractalPawn::LookUp(float Value)
{
    if (FMath::IsNearlyZero(Value))
    {
        return;
    }

    const float AngleRad = FMath::DegreesToRadians(-Value * LookSensitivity);
    const FQuat Delta = FQuat(GetActorRightVector(), AngleRad);
    const FQuat NewRotation = Delta * GetActorQuat();
    SetActorRotation(NewRotation);

}

void AFractalPawn::Roll(float Value)
{
    if (FMath::IsNearlyZero(Value))
    {
        return;
    }

    const float AngleRad = FMath::DegreesToRadians(-Value * LookSensitivity);
    const FQuat Delta = FQuat(GetActorForwardVector(), AngleRad);
    const FQuat NewRotation = Delta * GetActorQuat();
    SetActorRotation(NewRotation);

}
