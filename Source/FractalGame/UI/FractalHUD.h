// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FractalParameter.h"
#include "FractalMath/FractalScalar.h"
#include "FractalHUD.generated.h"

/**
 * Navigation and telemetry panels, fractal type wheel, power / scale bars and the controls overlay (while H is
 * held). AFractalPlayerController writes the public state every tick. Distances and speeds are given at the
 * unzoomed scale (the units of version 1.1), so they keep their meaning at any zoom depth.
 */
UCLASS()
class FRACTALGAME_API AFractalHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	float ZoomLevel = 0.0f;                         // 0..100% of the reachable zoom range
	double Magnification = 1.0;                     // relative to the unzoomed view
	float SpeedPercent = 0.0f;
	double Distance = 0.0;                          // camera to surface (cm)
	double MaxSpeed = 0.0;                          // current speed limit (cm/s)
	FVector CurrentVelocity = FVector::ZeroVector;  // cm/s
	FractalMath::FDDVec3 FractalPosition;           // camera position in fractal space
	double FractalScale = 1.0e-3;                   // fractal units per world unit (cm)
	bool bShowDebug = true;
	bool bShowHelp = false;

	EFractalType CurrentFractalType = EFractalType::Mandelbulb;
	float CurrentPower = 8.0f;
	float CurrentScaleMultiplier = 0.001f;

private:
	EFractalType PreviousFractalType = EFractalType::Mandelbulb;
	float TypeTransitionProgress = 0.0f;
	double LastTransitionTime = 0.0;

	void DrawTopLeftInfo(float X, float Y, float UIScale);
	void DrawTopRightInfo(float X, float Y, float UIScale);
	void DrawFractalParameters(float X, float Y, float UIScale);
	void DrawControlsPanel(float CenterX, float CenterY, float UIScale);
	void DrawKeyboardLayout(float X, float Y, float KeySize, float KeySpacing, float UIScale);
	void DrawVerticalControls(float CenterX, float Y, float KeySize, float KeySpacing, float UIScale);
	void DrawMouseButtons(float X, float Y, float UIScale);
	void DrawOtherControls(float X, float Y, float KeySize, float KeySpacing, float UIScale);
	void DrawPanel(float X, float Y, float Width, float Height, const FLinearColor& Color);
	void DrawBox(float X, float Y, float Width, float Height, float Thickness, const FLinearColor& Color);
	void DrawCompactStatBar(float X, float Y, float Width, float Height, const FString& Label,
							float Fraction, const FString& ValueText, const FLinearColor& BarColor, float UIScale);
	void DrawText(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Scale);
	void DrawRightAlignedText(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Scale);
	void DrawCenteredText(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Scale);
	void DrawLabeledKey(float X, float Y, float Size, const FString& KeyLabel, const FString& ActionLabel,
						const FLinearColor& Color, float UIScale);
	void DrawMouseWheel(float X, float Y, float Width, float Height, const FLinearColor& Color, float UIScale);
};
