// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FractalRaymarchActor.generated.h"

class UTextureRenderTarget2D;

UCLASS()
class FRACTAL_API AFractalRaymarchActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AFractalRaymarchActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal")
	class UMaterialInterface* FractalMaterial;

private:
	class UMaterialInstanceDynamic* FractalMaterialInstance;
};
