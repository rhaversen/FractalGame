// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalPawn.h"
#include "Components/SphereComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/FloatingPawnMovement.h"

AFractalPawn::AFractalPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->InitSphereRadius(40.0f);
	Collision->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);

	// The camera sits at the pawn's origin, which is the pivot of every rotation: the controller measures the
	// fractal distance at the camera, and turning must not swing the view around an offset pivot (deep in the
	// zoom the surface is only centimetres away). It follows the pawn's rotation, which the controller sets
	// directly.
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Collision);
	Camera->bUsePawnControlRotation = false;

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 1200.0f;
	Movement->Acceleration = 4096.0f;
	Movement->Deceleration = 4096.0f;

	// The controller sets the pawn's rotation directly; following the control rotation would undo it every frame.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
}
