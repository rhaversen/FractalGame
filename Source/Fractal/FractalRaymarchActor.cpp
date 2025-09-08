// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalRaymarchActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"

// Sets default values
AFractalRaymarchActor::AFractalRaymarchActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AFractalRaymarchActor::BeginPlay()
{
	Super::BeginPlay();

	if (FractalMaterial)
	{
		FractalMaterialInstance = UMaterialInstanceDynamic::Create(FractalMaterial, this);
	}
	
}

// Called every frame
void AFractalRaymarchActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (FractalMaterialInstance && RenderTarget)
	{
		// Pass camera parameters to the material instance
		APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
		if (PlayerController)
		{
			FVector CameraLocation;
			FRotator CameraRotation;
			PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

			FractalMaterialInstance->SetVectorParameterValue(TEXT("CameraOrigin"), FLinearColor(CameraLocation));
			FractalMaterialInstance->SetVectorParameterValue(TEXT("CameraForward"), FLinearColor(CameraRotation.Vector()));
		}

		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, RenderTarget, FractalMaterialInstance);
	}
}
