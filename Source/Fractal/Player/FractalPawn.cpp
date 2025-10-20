// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPawn.h"
#include "Components/SphereComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/FloatingPawnMovement.h"

AFractalPawn::AFractalPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->InitSphereRadius(40.0f);
	Collision->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Collision);
	Camera->bUsePawnControlRotation = true;
	Camera->SetRelativeLocation(FVector(0, 0, 40));

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 1200.0f;
	Movement->Acceleration = 4096.0f;
	Movement->Deceleration = 4096.0f;

	// Use controller rotation so camera reflects Pan/Tilt/Roll inputs
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = true;
	bUseControllerRotationRoll = true;
}

void AFractalPawn::BeginPlay()
{
	Super::BeginPlay();
}

void AFractalPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}
