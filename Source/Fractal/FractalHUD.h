// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FractalHUD.generated.h"

UCLASS()
class AFractalHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	// Debug display data (updated by PlayerController)
	float ZoomLevel = 0.0f;
	float SpeedPercent = 0.0f;
	FVector LocalPos = FVector::ZeroVector;
	bool bShowDebug = true;
	bool bShowHelp = false;

	// Optional: Texture/Material rendering for future use
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	UTexture2D* OverlayTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	UMaterialInterface* HUDMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	UTextureRenderTarget2D* RenderTarget = nullptr;

private:
	FString CreateBar(float Percent, int32 Width = 20) const;
};
