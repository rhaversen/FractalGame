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

	float ZoomLevel = 0.0f;
	float SpeedPercent = 0.0f;
	float Distance = 0.0f;
	float MaxSpeed = 0.0f;
	FVector LocalPos = FVector::ZeroVector;
	FVector CurrentVelocity = FVector::ZeroVector;
	bool bShowDebug = true;
	bool bShowHelp = false;
	
	int32 CurrentFractalType = 0;
	float CurrentPower = 8.0f;
	
	int32 PreviousFractalType = 0;
	float TypeTransitionProgress = 0.0f;
	float LastTransitionTime = 0.0f;

private:
	void DrawTopLeftInfo(float X, float Y, float UIScale);
	void DrawTopRightInfo(float X, float Y, float UIScale);
	void DrawBottomLeftInfo(float X, float Y, float UIScale);
	void DrawFractalParameters(float X, float Y, float UIScale);
	void DrawInfoPanel(float X, float Y, float UIScale);
	void DrawControlsPanel(float CenterX, float CenterY, float UIScale);
	void DrawKeyboardLayout(float X, float Y, float KeySize, float KeySpacing, float UIScale);
	void DrawVerticalControls(float CenterX, float Y, float KeySize, float KeySpacing, float UIScale);
	void DrawMouseControls(float X, float Y, float UIScale);
	void DrawMouseButtons(float X, float Y, float UIScale);
	void DrawOtherControls(float X, float Y, float KeySize, float KeySpacing, float UIScale);
	void DrawPanel(float X, float Y, float Width, float Height, const FLinearColor &Color);
	void DrawBox(float X, float Y, float Width, float Height, float Thickness, const FLinearColor &Color);
	void DrawCompactStatBar(float X, float Y, float Width, float Height, const FString &Label,
							float Value, float MaxValue, const FString &Unit, const FLinearColor &BarColor, float UIScale);
	void DrawStatBar(float X, float Y, float Width, float Height, const FString &Label,
					 float Value, float MaxValue, const FLinearColor &BarColor, float UIScale, bool ShowValue = false);
	void DrawGradientBar(float X, float Y, float Width, float Height,
						 const FLinearColor &StartColor, const FLinearColor &EndColor);
	void DrawRightAlignedMetric(float X, float Y, const FString &Label,
								float Value, const FString &Unit, const FLinearColor &Color, float UIScale);
	void DrawRightAlignedPosition(float X, float Y, float UIScale);
	void DrawRightAlignedText(const FString &Text, const FVector2D &Position,
							  const FLinearColor &Color, float Scale);
	void DrawMetricDisplay(float X, float Y, float Width, const FString &Label,
						   float Value, const FString &Unit, const FLinearColor &Color, float UIScale);
	void DrawPositionDisplay(float X, float Y, float Width, float UIScale);
	void DrawText(const FString &Text, const FVector2D &Position,
				  const FLinearColor &Color, float Scale);
	void DrawTextWithShadow(const FString &Text, const FVector2D &Position,
							const FLinearColor &Color, float Scale);
	void DrawCenteredText(const FString &Text, const FVector2D &Position,
						  const FLinearColor &Color, float Scale);
	void DrawKeyGraphic(float X, float Y, const FString &Keys, const FLinearColor &Color, float UIScale);
	void DrawKeyButton(float X, float Y, float Width, float Height, const FString &Label,
					   const FLinearColor &Color, float UIScale);
	void DrawLabeledKey(float X, float Y, float Size, const FString &KeyLabel, const FString &ActionLabel,
						const FLinearColor &Color, float UIScale);
	void DrawMouseWheel(float X, float Y, float Width, float Height, const FLinearColor &Color, float UIScale);
};
