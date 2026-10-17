// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalHUD.h"
#include "FractalControlSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "CanvasItem.h"

namespace
{
	const TCHAR* const BuildLabel = TEXT("FRACTAL EXPLORER v2.0");
	const TCHAR* const HintLabel = TEXT("[ H ] CONTROLS  •  [ R ] RESET");

	float RangeFraction(float Value, float MinValue, float MaxValue)
	{
		return FMath::Clamp((Value - MinValue) / FMath::Max(MaxValue - MinValue, UE_KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	}

	// Fixed-point with the given decimals while the value does not round to zero, scientific below
	// (deep in the zoom distances and speeds shrink to ~1e-27 of their unzoomed size).
	FString FormatQuantity(double Value, int32 Decimals, const TCHAR* Unit)
	{
		if (Value == 0.0 || FMath::Abs(Value) >= 0.5 * FMath::Pow(10.0, -Decimals))
		{
			return FString::Printf(TEXT("%.*f %s"), Decimals, Value, Unit);
		}
		return FString::Printf(TEXT("%.2e %s"), Value, Unit);
	}

	FString FormatMagnification(double Magnification)
	{
		return Magnification < 1000.0 ? FString::Printf(TEXT("x%.1f"), Magnification) : FString::Printf(TEXT("x%.2e"), Magnification);
	}

	// Explicit sign like the original "%+" formatting, so the columns line up.
	FString FormatSignedCoordinate(const FractalMath::FDD& Value, int32 Decimals)
	{
		const FString Digits = UFractalControlSubsystem::FormatCoordinate(Value, Decimals);
		return FString::Printf(TEXT("%s%s"), Value.Hi < 0.0 ? TEXT("") : TEXT("+"), *Digits);
	}
}

void AFractalHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const float ReferenceHeight = 1080.0f;
	const float ScaleFactor = Canvas->SizeY / ReferenceHeight;
	const float UIScale = FMath::Clamp(ScaleFactor, 0.5f, 2.0f);

	const float MarginX = 30.0f * UIScale;
	const float MarginY = 30.0f * UIScale;

	if (bShowDebug)
	{
		DrawTopLeftInfo(MarginX, MarginY, UIScale);
		DrawTopRightInfo(Canvas->SizeX - MarginX, MarginY, UIScale);
	}

	DrawFractalParameters(Canvas->SizeX - MarginX, Canvas->SizeY - MarginY, UIScale);

	if (bShowHelp)
	{
		DrawControlsPanel(Canvas->SizeX / 2, Canvas->SizeY / 2, UIScale);
	}
}

void AFractalHUD::DrawTopLeftInfo(float X, float Y, float UIScale)
{
	float CurrentY = Y;
	const float LineSpacing = 36.0f * UIScale;

	DrawText(TEXT("NAVIGATION"), FVector2D(X, CurrentY),
			 FLinearColor(0.4f, 0.8f, 1.0f, 0.95f), UIScale * 1.3f);
	CurrentY += LineSpacing * 1.2f;

	const float BarWidth = 280.0f * UIScale;
	const float BarHeight = 8.0f * UIScale;

	const FString ZoomText = FString::Printf(TEXT("%.1f %%  %s"), ZoomLevel, *FormatMagnification(Magnification));
	DrawCompactStatBar(X, CurrentY, BarWidth, BarHeight, TEXT("ZOOM"), ZoomLevel / 100.0f, ZoomText,
					   FLinearColor(1.0f, 0.6f, 0.2f, 0.9f), UIScale);
	CurrentY += LineSpacing;

	DrawCompactStatBar(X, CurrentY, BarWidth, BarHeight, TEXT("SPEED LIMIT"), SpeedPercent / 100.0f,
					   FString::Printf(TEXT("%.1f %%"), SpeedPercent), FLinearColor(0.3f, 1.0f, 0.5f, 0.9f), UIScale);
	CurrentY += LineSpacing;

	const double VelMagnitude = CurrentVelocity.Size() * 0.01;
	const double VelMax = MaxSpeed * 0.01;
	const float VelFraction = VelMax > 0.0 ? static_cast<float>(VelMagnitude / VelMax) : 0.0f;
	DrawCompactStatBar(X, CurrentY, BarWidth, BarHeight, TEXT("VELOCITY"), VelFraction, FormatQuantity(VelMagnitude, 1, TEXT("m/s")),
					   FLinearColor(0.4f, 0.9f, 1.0f, 0.9f), UIScale);
}

void AFractalHUD::DrawTopRightInfo(float X, float Y, float UIScale)
{
	float CurrentY = Y;
	const float LineSpacing = 36.0f * UIScale;
	const FLinearColor LabelColor(0.7f, 0.7f, 0.8f, 0.95f);

	DrawText(TEXT("TELEMETRY"), FVector2D(X - 320.0f * UIScale, CurrentY),
			 FLinearColor(1.0f, 0.8f, 0.4f, 0.95f), UIScale * 1.3f);
	CurrentY += LineSpacing * 1.2f;

	DrawText(TEXT("DIST TO FRACTAL"), FVector2D(X - 320.0f * UIScale, CurrentY), LabelColor, UIScale * 1.1f);
	DrawRightAlignedText(FormatQuantity(Distance * 0.01, 4, TEXT("m")), FVector2D(X, CurrentY),
						 FLinearColor(1.0f, 0.9f, 0.3f, 0.95f), UIScale * 1.1f);
	CurrentY += LineSpacing;

	// Fractal coordinates, with enough decimals that they still change when the camera moves a fraction of a
	// centimetre (one centimetre is FractalScale fractal units).
	DrawText(TEXT("POSITION"), FVector2D(X - 320.0f * UIScale, CurrentY), LabelColor, UIScale * 1.1f);
	const int32 Decimals = FMath::Clamp(FMath::CeilToInt32(-FMath::Loge(FractalScale) / FMath::Loge(10.0)) + 2, 4, 30);
	const FString PositionLines[3] = {
		FString::Printf(TEXT("X %s"), *FormatSignedCoordinate(FractalPosition.X, Decimals)),
		FString::Printf(TEXT("Y %s"), *FormatSignedCoordinate(FractalPosition.Y, Decimals)),
		FString::Printf(TEXT("Z %s"), *FormatSignedCoordinate(FractalPosition.Z, Decimals)),
	};
	float LineY = CurrentY + 24.0f * UIScale;
	for (const FString& Line : PositionLines)
	{
		DrawRightAlignedText(Line, FVector2D(X, LineY), FLinearColor(0.7f, 0.5f, 1.0f, 0.95f), UIScale * 0.95f);
		LineY += 22.0f * UIScale;
	}
	CurrentY = LineY + 14.0f * UIScale;

	// Everything needed to find this spot again: the formula and its parameters, the zoom exactly (the coordinate
	// decimals above follow from it), and which way the camera looks (forward and up fix the roll as well).
	DrawText(TEXT("VIEW"), FVector2D(X - 320.0f * UIScale, CurrentY), FLinearColor(1.0f, 0.8f, 0.4f, 0.95f), UIScale * 1.3f);
	CurrentY += LineSpacing * 1.0f;
	const FLinearColor ValueColor(0.8f, 0.9f, 1.0f, 0.95f);
	const auto DrawRow = [&](const TCHAR* Label, const FString& Value)
	{
		DrawText(Label, FVector2D(X - 320.0f * UIScale, CurrentY), LabelColor, UIScale * 0.95f);
		DrawRightAlignedText(Value, FVector2D(X, CurrentY), ValueColor, UIScale * 0.95f);
		CurrentY += 22.0f * UIScale;
	};
	DrawRow(TEXT("FRACTAL"), UFractalControlSubsystem::GetFractalDisplayName(CurrentFractalType));
	DrawRow(TEXT("POWER"), FString::Printf(TEXT("%.6f"), CurrentPower));
	DrawRow(TEXT("USER SCALE"), FString::Printf(TEXT("%.6e / cm"), UserScale));
	DrawRow(TEXT("ZOOM"), FString::Printf(TEXT("x%.9e"), Magnification));
	DrawRow(TEXT("SCALE"), FString::Printf(TEXT("%.9e / cm"), FractalScale));
	DrawRow(TEXT("DECIMALS"), FString::Printf(TEXT("%d"), Decimals));
	DrawRow(TEXT("FACING"), FString::Printf(TEXT("%+.9f %+.9f %+.9f"), ViewForward.X, ViewForward.Y, ViewForward.Z));
	DrawRow(TEXT("UP"), FString::Printf(TEXT("%+.9f %+.9f %+.9f"), ViewUp.X, ViewUp.Y, ViewUp.Z));
	DrawRow(TEXT("PITCH YAW ROLL"), FString::Printf(TEXT("%+.4f %+.4f %+.4f"), ViewRotation.Pitch, ViewRotation.Yaw, ViewRotation.Roll));
}

void AFractalHUD::DrawFractalParameters(float X, float Y, float UIScale)
{
	const float ScreenWidth = Canvas->SizeX;
	const float Margin = 30.0f * UIScale;
	const float BottomMargin = 15.0f * UIScale;
	const float StatBarWidth = 280.0f * UIScale;
	const float StatBarHeight = 8.0f * UIScale;
	const float StatLineSpacing = 36.0f * UIScale;
	const float BottomAlignY = Y - BottomMargin - (StatLineSpacing - StatBarHeight);
	const FractalMath::FFractalPreset& Preset = UFractalControlSubsystem::GetPreset(CurrentFractalType);

	// Update transition animation
	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (CurrentFractalType != PreviousFractalType)
	{
		if (LastTransitionTime == 0.0 || CurrentTime - LastTransitionTime > 0.5)
		{
			LastTransitionTime = CurrentTime;
			TypeTransitionProgress = 0.0f;
		}
	}

	if (TypeTransitionProgress < 1.0f)
	{
		TypeTransitionProgress = FMath::Clamp(static_cast<float>(CurrentTime - LastTransitionTime) / 0.3f, 0.0f, 1.0f);

		if (TypeTransitionProgress >= 1.0f)
		{
			PreviousFractalType = CurrentFractalType;
		}
	}

	// Smooth easing function
	const float EasedProgress = TypeTransitionProgress * TypeTransitionProgress * (3.0f - 2.0f * TypeTransitionProgress);

	// Type wheel in bottom-right corner
	const float TypePanelWidth = 240.0f * UIScale;
	const float TypePanelHeight = 120.0f * UIScale;
	const float TypePanelX = X - TypePanelWidth;
	const float TypePanelY = BottomAlignY - TypePanelHeight;
	const float Padding = 12.0f * UIScale;

	DrawPanel(TypePanelX, TypePanelY, TypePanelWidth, TypePanelHeight, FLinearColor(0.01f, 0.01f, 0.03f, 0.75f));

	DrawText(TEXT("TYPE"), FVector2D(TypePanelX + Padding, TypePanelY + Padding),
			 FLinearColor(0.5f, 0.5f, 0.6f, 0.9f), UIScale * 1.0f);

	const float ListCenterBase = TypePanelY + (TypePanelHeight * 0.5f);
	const float WheelCenterY = ListCenterBase + 8.0f * UIScale;
	const float ItemSpacing = 35.0f * UIScale;
	const float PrevItemSpacing = ItemSpacing * 0.65f;
	const float TransitionOffset = ItemSpacing * EasedProgress;
	const float NameX = TypePanelX + Padding + 8.0f * UIScale;

	const int32 NumTypes = static_cast<int32>(EFractalType::Count);
	const int32 CurrentIndex = static_cast<int32>(CurrentFractalType);
	const EFractalType PrevType = static_cast<EFractalType>((CurrentIndex - 1 + NumTypes) % NumTypes);
	const EFractalType NextType = static_cast<EFractalType>((CurrentIndex + 1) % NumTypes);

	// Colors for interpolation
	const FLinearColor FadedColor = FLinearColor(0.3f, 0.3f, 0.4f, 0.4f);
	const FLinearColor BrightColor = FLinearColor(0.3f, 1.0f, 0.5f, 1.0f);

	// Scales for interpolation
	const float FadedScale = 0.8f;
	const float BrightScale = 1.1f;

	// Draw previous (above, fading out and shrinking)
	const float PrevY = WheelCenterY - PrevItemSpacing - TransitionOffset;
	if (PrevY >= TypePanelY - 20.0f * UIScale && PrevY <= TypePanelY + TypePanelHeight)
	{
		const float PrevScale = FMath::Lerp(BrightScale, FadedScale, EasedProgress);
		const FLinearColor PrevColor = FLinearColor::LerpUsingHSV(BrightColor, FadedColor, EasedProgress);
		DrawText(UFractalControlSubsystem::GetFractalDisplayName(PrevType), FVector2D(NameX, PrevY), PrevColor, UIScale * PrevScale);
	}

	// Draw current (center, transitioning from faded to bright)
	const float CurY = WheelCenterY - TransitionOffset;
	if (CurY >= TypePanelY - 20.0f * UIScale && CurY <= TypePanelY + TypePanelHeight + 20.0f * UIScale)
	{
		const float CurScale = FMath::Lerp(FadedScale, BrightScale, EasedProgress);
		const FLinearColor CurColor = FLinearColor::LerpUsingHSV(FadedColor, BrightColor, EasedProgress);
		DrawText(UFractalControlSubsystem::GetFractalDisplayName(CurrentFractalType), FVector2D(NameX, CurY), CurColor, UIScale * CurScale);
	}

	// Draw next (below, transitioning from faded to current position)
	const float NextY = WheelCenterY + ItemSpacing - TransitionOffset;
	if (NextY >= TypePanelY && NextY <= TypePanelY + TypePanelHeight + 20.0f * UIScale)
	{
		DrawText(UFractalControlSubsystem::GetFractalDisplayName(NextType), FVector2D(NameX, NextY), FadedColor, UIScale * FadedScale);
	}

	// Hints and version centered along bottom alignment
	if (!bShowHelp)
	{
		const float HintSpacing = 8.0f * UIScale;
		FVector2D TitleSize(0.0f, 0.0f);
		FVector2D HintSize(0.0f, 0.0f);
		Canvas->TextSize(GEngine->GetMediumFont(), BuildLabel, TitleSize.X, TitleSize.Y, UIScale * 0.85f, UIScale * 0.85f);
		Canvas->TextSize(GEngine->GetMediumFont(), HintLabel, HintSize.X, HintSize.Y, UIScale * 0.9f, UIScale * 0.9f);
		const float HintTopY = BottomAlignY - (TitleSize.Y + HintSpacing + HintSize.Y);

		DrawCenteredText(BuildLabel, FVector2D(ScreenWidth / 2, HintTopY),
						 FLinearColor(0.4f, 0.4f, 0.5f, 0.5f), UIScale * 0.85f);

		DrawCenteredText(HintLabel, FVector2D(ScreenWidth / 2, HintTopY + TitleSize.Y + HintSpacing),
						 FLinearColor(0.5f, 0.7f, 0.9f, 0.6f), UIScale * 0.9f);
	}

	// Power and scale bars in bottom-left corner (mirrors top-left stats)
	const float StatX = Margin;
	const float StatY = BottomAlignY - StatLineSpacing - StatBarHeight;

	const float PowerDisplay = FMath::Clamp(CurrentPower, Preset.MinPower, Preset.MaxPower);
	DrawCompactStatBar(StatX, StatY, StatBarWidth, StatBarHeight, TEXT("POWER"),
					   RangeFraction(CurrentPower, Preset.MinPower, Preset.MaxPower), FString::Printf(TEXT("%.1f"), PowerDisplay),
					   FLinearColor(1.0f, 0.6f, 0.2f, 0.9f), UIScale);

	const float ScaleDisplay = FMath::Clamp(CurrentScaleMultiplier, Preset.MinScale, Preset.MaxScale) * 1000.0f;
	DrawCompactStatBar(StatX, StatY + StatLineSpacing, StatBarWidth, StatBarHeight, TEXT("SCALE"),
					   RangeFraction(CurrentScaleMultiplier, Preset.MinScale, Preset.MaxScale), FString::Printf(TEXT("%.2f x10^-3"), ScaleDisplay),
					   FLinearColor(0.3f, 0.9f, 1.0f, 0.9f), UIScale);
}

void AFractalHUD::DrawControlsPanel(float CenterX, float CenterY, float UIScale)
{
	const float PanelWidth = 850.0f * UIScale;
	const float PanelHeight = 750.0f * UIScale;
	const float X = CenterX - PanelWidth / 2;
	const float Y = CenterY - PanelHeight / 2;
	const float Padding = 45.0f * UIScale;

	DrawPanel(X, Y, PanelWidth, PanelHeight, FLinearColor(0.02f, 0.02f, 0.05f, 0.95f));

	float CurrentY = Y + Padding;

	DrawCenteredText(TEXT("CONTROLS"), FVector2D(CenterX, CurrentY),
					 FLinearColor(0.4f, 0.9f, 1.0f, 1.0f), UIScale * 2.5f);
	CurrentY += 80.0f * UIScale;

	const float KeySize = 80.0f * UIScale;
	const float KeySpacing = 14.0f * UIScale;

	DrawText(TEXT("MOVEMENT & ROLL"), FVector2D(CenterX - 110.0f * UIScale, CurrentY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 1.4f);
	CurrentY += 38.0f * UIScale;

	DrawKeyboardLayout(CenterX - 115.0f * UIScale, CurrentY, KeySize, KeySpacing, UIScale);
	CurrentY += KeySize * 2 + KeySpacing + 35.0f * UIScale;

	DrawVerticalControls(CenterX, CurrentY, KeySize, KeySpacing, UIScale);
	CurrentY += KeySize + 62.0f * UIScale;

	DrawText(TEXT("FRACTAL & OTHER"), FVector2D(CenterX - 95.0f * UIScale, CurrentY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 1.4f);
	CurrentY += 38.0f * UIScale;

	const float BottomSectionY = CurrentY;
	const float ItemSpacing = 100.0f * UIScale;

	// Left: Scroll wheel
	DrawMouseWheel(CenterX - ItemSpacing * 2.5f - 25.0f * UIScale, BottomSectionY, 50.0f * UIScale, 70.0f * UIScale,
				   FLinearColor(1.0f, 0.6f, 0.9f, 1.0f), UIScale);
	DrawText(TEXT("Speed Limit"), FVector2D(CenterX - ItemSpacing * 2.5f - 48.0f * UIScale, BottomSectionY + 80.0f * UIScale),
			 FLinearColor(1.0f, 0.6f, 0.9f, 1.0f), UIScale * 0.85f);

	// Mouse buttons (stacked)
	DrawMouseButtons(CenterX - ItemSpacing * 1.3f, BottomSectionY, UIScale);

	// Keyboard controls in a row
	DrawOtherControls(CenterX + ItemSpacing * 0.3f, BottomSectionY, KeySize * 0.85f, KeySpacing, UIScale);
}

void AFractalHUD::DrawKeyboardLayout(float X, float Y, float KeySize, float KeySpacing, float UIScale)
{
	const FLinearColor RollColor = FLinearColor(1.0f, 0.8f, 0.4f, 1.0f);
	const FLinearColor MoveColor = FLinearColor(0.4f, 1.0f, 0.7f, 1.0f);

	DrawLabeledKey(X, Y, KeySize, TEXT("Q"), TEXT("ROLL\nLEFT"), RollColor, UIScale);
	DrawLabeledKey(X + KeySize + KeySpacing, Y, KeySize, TEXT("W"), TEXT("FORWARD"), MoveColor, UIScale);
	DrawLabeledKey(X + (KeySize + KeySpacing) * 2, Y, KeySize, TEXT("E"), TEXT("ROLL\nRIGHT"), RollColor, UIScale);

	DrawLabeledKey(X, Y + KeySize + KeySpacing, KeySize, TEXT("A"), TEXT("STRAFE\nLEFT"), MoveColor, UIScale);
	DrawLabeledKey(X + KeySize + KeySpacing, Y + KeySize + KeySpacing, KeySize, TEXT("S"), TEXT("BACK"), MoveColor, UIScale);
	DrawLabeledKey(X + (KeySize + KeySpacing) * 2, Y + KeySize + KeySpacing, KeySize, TEXT("D"), TEXT("STRAFE\nRIGHT"), MoveColor, UIScale);
}

void AFractalHUD::DrawVerticalControls(float CenterX, float Y, float KeySize, float KeySpacing, float UIScale)
{
	const FLinearColor VertColor = FLinearColor(0.5f, 0.8f, 1.0f, 1.0f);
	const float SpacebarWidth = KeySize * 3.5f;
	const float TotalWidth = KeySize + SpacebarWidth + KeySpacing;
	const float StartX = CenterX - TotalWidth / 2;

	DrawLabeledKey(StartX, Y, KeySize, TEXT("SHIFT"), TEXT("DESCEND"), VertColor, UIScale);

	const float SpacebarX = StartX + KeySize + KeySpacing;

	FCanvasTileItem Background(FVector2D(SpacebarX, Y), FVector2D(SpacebarWidth, KeySize), FLinearColor(0.05f, 0.05f, 0.1f, 0.6f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	FLinearColor HighlightColor = VertColor;
	HighlightColor.A = 0.2f;
	FCanvasTileItem Highlight(FVector2D(SpacebarX, Y), FVector2D(SpacebarWidth, KeySize * 0.35f), HighlightColor);
	Highlight.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Highlight);

	DrawBox(SpacebarX, Y, SpacebarWidth, KeySize, 2.0f, VertColor);

	FVector2D KeyTextSize;
	const float KeyTextScale = UIScale * 1.8f;
	Canvas->TextSize(GEngine->GetMediumFont(), TEXT("SPACE"), KeyTextSize.X, KeyTextSize.Y, KeyTextScale, KeyTextScale);
	const FVector2D KeyTextPos(SpacebarX + (SpacebarWidth - KeyTextSize.X) / 2, Y + KeySize * 0.20f);
	DrawText(TEXT("SPACE"), KeyTextPos, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), KeyTextScale);

	const float ActionTextScale = UIScale * 0.85f;
	FVector2D ActionTextSize;
	Canvas->TextSize(GEngine->GetMediumFont(), TEXT("ASCEND"), ActionTextSize.X, ActionTextSize.Y, ActionTextScale, ActionTextScale);
	const FVector2D ActionTextPos(SpacebarX + (SpacebarWidth - ActionTextSize.X) / 2, Y + KeySize * 0.62f);
	DrawText(TEXT("ASCEND"), ActionTextPos, VertColor, ActionTextScale);
}

void AFractalHUD::DrawMouseButtons(float X, float Y, float UIScale)
{
	const FLinearColor ButtonColor = FLinearColor(0.7f, 0.7f, 0.8f, 0.9f);
	const FLinearColor MouseColor = FLinearColor(1.0f, 0.6f, 0.9f, 1.0f);
	const FLinearColor ScaleColor = FLinearColor(0.3f, 0.9f, 1.0f, 1.0f);

	float ButtonY = Y;

	DrawText(TEXT("LEFT CLICK"), FVector2D(X - 45.0f * UIScale, ButtonY), ButtonColor, UIScale * 0.95f);
	DrawText(TEXT("Decrease Power"), FVector2D(X - 60.0f * UIScale, ButtonY + 18.0f * UIScale), MouseColor, UIScale * 0.75f);

	ButtonY += 42.0f * UIScale;

	DrawText(TEXT("RIGHT CLICK"), FVector2D(X - 50.0f * UIScale, ButtonY), ButtonColor, UIScale * 0.95f);
	DrawText(TEXT("Increase Power"), FVector2D(X - 60.0f * UIScale, ButtonY + 18.0f * UIScale), MouseColor, UIScale * 0.75f);

	ButtonY += 42.0f * UIScale;

	DrawText(TEXT("MOUSE BACK"), FVector2D(X - 50.0f * UIScale, ButtonY), ButtonColor, UIScale * 0.95f);
	DrawText(TEXT("Decrease Scale"), FVector2D(X - 60.0f * UIScale, ButtonY + 18.0f * UIScale), ScaleColor, UIScale * 0.75f);

	ButtonY += 42.0f * UIScale;

	DrawText(TEXT("MOUSE FORWARD"), FVector2D(X - 60.0f * UIScale, ButtonY), ButtonColor, UIScale * 0.95f);
	DrawText(TEXT("Increase Scale"), FVector2D(X - 60.0f * UIScale, ButtonY + 18.0f * UIScale), ScaleColor, UIScale * 0.75f);
}

void AFractalHUD::DrawMouseWheel(float X, float Y, float Width, float Height, const FLinearColor& Color, float UIScale)
{
	const FLinearColor BackColor = FLinearColor(0.05f, 0.05f, 0.1f, 0.6f);
	const float Radius = Width / 2.0f;

	const float CenterX = X + Width / 2.0f;
	const float TopCapY = Y + Radius;
	const float BottomCapY = Y + Height - Radius;

	// Capsule outline: straight sides between the two half-circle caps
	const auto IsInside = [&](float PX, float PY)
	{
		if (PY >= TopCapY && PY <= BottomCapY)
		{
			return true;
		}
		const float CapY = PY < TopCapY ? TopCapY : BottomCapY;
		return FMath::Sqrt(FMath::Square(PX - CenterX) + FMath::Square(PY - CapY)) <= Radius;
	};

	for (float DY = 0; DY < Height; DY += 1.0f)
	{
		for (float DX = 0; DX < Width; DX += 2.0f)
		{
			if (IsInside(X + DX, Y + DY))
			{
				FCanvasTileItem Pixel(FVector2D(X + DX, Y + DY), FVector2D(2.0f, 1.0f), BackColor);
				Pixel.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Pixel);
			}
		}
	}

	FLinearColor HighlightColor = Color;
	HighlightColor.A = 0.2f;
	const float HighlightHeight = Height * 0.35f;
	for (float DY = 0; DY < HighlightHeight; DY += 1.0f)
	{
		for (float DX = 0; DX < Width; DX += 2.0f)
		{
			if (IsInside(X + DX, Y + DY))
			{
				FCanvasTileItem Pixel(FVector2D(X + DX, Y + DY), FVector2D(2.0f, 1.0f), HighlightColor);
				Pixel.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Pixel);
			}
		}
	}

	const float BorderThickness = 2.0f;
	for (float DY = 0; DY < Height; DY += 1.0f)
	{
		for (float DX = 0; DX < Width; DX += 1.0f)
		{
			const float PX = X + DX;
			const float PY = Y + DY;

			float DistFromEdge;
			if (PY >= TopCapY && PY <= BottomCapY)
			{
				DistFromEdge = FMath::Min(FMath::Abs(DX), FMath::Abs(DX - Width));
			}
			else
			{
				const float CapY = PY < TopCapY ? TopCapY : BottomCapY;
				DistFromEdge = FMath::Abs(FMath::Sqrt(FMath::Square(PX - CenterX) + FMath::Square(PY - CapY)) - Radius);
			}

			if (DistFromEdge <= BorderThickness)
			{
				FCanvasTileItem BorderPixel(FVector2D(PX, PY), FVector2D(1.0f, 1.0f), Color);
				BorderPixel.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(BorderPixel);
			}
		}
	}
}

void AFractalHUD::DrawOtherControls(float X, float Y, float KeySize, float KeySpacing, float UIScale)
{
	const FLinearColor ResetColor = FLinearColor(1.0f, 0.5f, 0.5f, 1.0f);
	const FLinearColor HelpColor = FLinearColor(0.7f, 0.7f, 1.0f, 1.0f);
	const FLinearColor FractalColor = FLinearColor(0.3f, 1.0f, 0.5f, 1.0f);

	// All keys in one row
	DrawLabeledKey(X, Y, KeySize, TEXT("R"), TEXT("RESET"), ResetColor, UIScale);
	DrawLabeledKey(X + KeySize + KeySpacing, Y, KeySize, TEXT("H"), TEXT("HELP"), HelpColor, UIScale);
	DrawLabeledKey(X + (KeySize + KeySpacing) * 2, Y, KeySize, TEXT("TAB"), TEXT("CYCLE"), FractalColor, UIScale);
}

void AFractalHUD::DrawPanel(float X, float Y, float Width, float Height, const FLinearColor& Color)
{
	FCanvasTileItem BackPanel(FVector2D(X, Y), FVector2D(Width, Height), Color);
	BackPanel.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(BackPanel);

	const float BorderThickness = 3.0f;
	DrawBox(X, Y, Width, Height, BorderThickness, FLinearColor(0.3f, 0.7f, 1.0f, 0.8f));

	const float InnerGlow = 8.0f;
	DrawBox(X + BorderThickness, Y + BorderThickness,
			Width - BorderThickness * 2, Height - BorderThickness * 2, InnerGlow, FLinearColor(0.2f, 0.5f, 0.8f, 0.2f));
}

void AFractalHUD::DrawBox(float X, float Y, float Width, float Height, float Thickness, const FLinearColor& Color)
{
	FCanvasTileItem Top(FVector2D(X, Y), FVector2D(Width, Thickness), Color);
	FCanvasTileItem Bottom(FVector2D(X, Y + Height - Thickness), FVector2D(Width, Thickness), Color);
	FCanvasTileItem Left(FVector2D(X, Y), FVector2D(Thickness, Height), Color);
	FCanvasTileItem Right(FVector2D(X + Width - Thickness, Y), FVector2D(Thickness, Height), Color);

	Top.BlendMode = Bottom.BlendMode = Left.BlendMode = Right.BlendMode = SE_BLEND_Translucent;

	Canvas->DrawItem(Top);
	Canvas->DrawItem(Bottom);
	Canvas->DrawItem(Left);
	Canvas->DrawItem(Right);
}

void AFractalHUD::DrawCompactStatBar(float X, float Y, float Width, float Height, const FString& Label,
									 float Fraction, const FString& ValueText, const FLinearColor& BarColor, float UIScale)
{
	const float LabelY = Y - 22.0f * UIScale;
	DrawText(Label, FVector2D(X, LabelY), FLinearColor(0.8f, 0.8f, 0.9f, 0.95f), UIScale * 1.1f);

	FCanvasTileItem Background(FVector2D(X, Y), FVector2D(Width, Height), FLinearColor(0.05f, 0.05f, 0.1f, 0.6f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	const float FilledWidth = Width * FMath::Clamp(Fraction, 0.0f, 1.0f);
	if (FilledWidth > 2.0f)
	{
		FLinearColor GlowColor = BarColor;
		GlowColor.A = 0.3f;
		FCanvasTileItem Glow(FVector2D(X, Y - 2.0f), FVector2D(FilledWidth, Height + 4.0f), GlowColor);
		Glow.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Glow);

		FCanvasTileItem Bar(FVector2D(X, Y), FVector2D(FilledWidth, Height), BarColor);
		Bar.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Bar);
	}

	DrawBox(X, Y, Width, Height, 1.0f, FLinearColor(0.3f, 0.3f, 0.4f, 0.8f));

	DrawText(ValueText, FVector2D(X + Width + 12.0f * UIScale, Y - 6.0f * UIScale), BarColor, UIScale * 1.0f);
}

void AFractalHUD::DrawText(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Scale)
{
	FCanvasTextItem TextItem(Position, FText::FromString(Text), GEngine->GetMediumFont(), Color);
	TextItem.Scale = FVector2D(Scale, Scale);
	TextItem.bOutlined = false;
	TextItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(TextItem);
}

void AFractalHUD::DrawRightAlignedText(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Scale)
{
	FVector2D TextSize;
	Canvas->TextSize(GEngine->GetMediumFont(), Text, TextSize.X, TextSize.Y, Scale, Scale);

	DrawText(Text, FVector2D(Position.X - TextSize.X, Position.Y), Color, Scale);
}

void AFractalHUD::DrawCenteredText(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Scale)
{
	FCanvasTextItem TextItem(Position, FText::FromString(Text), GEngine->GetMediumFont(), Color);
	TextItem.Scale = FVector2D(Scale, Scale);
	TextItem.bCentreX = true;
	TextItem.bOutlined = false;
	TextItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(TextItem);
}

void AFractalHUD::DrawLabeledKey(float X, float Y, float Size, const FString& KeyLabel, const FString& ActionLabel, const FLinearColor& Color, float UIScale)
{
	FCanvasTileItem Background(FVector2D(X, Y), FVector2D(Size, Size), FLinearColor(0.08f, 0.08f, 0.12f, 0.9f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	const float BorderThickness = 2.5f * UIScale;
	FLinearColor BorderColor = Color;
	BorderColor.A = 0.9f;
	DrawBox(X, Y, Size, Size, BorderThickness, BorderColor);

	FLinearColor InnerBorderColor = Color;
	InnerBorderColor.A = 0.2f;
	DrawBox(X + BorderThickness, Y + BorderThickness,
			Size - BorderThickness * 2, Size - BorderThickness * 2, 1.5f * UIScale, InnerBorderColor);

	FLinearColor HighlightColor = Color;
	HighlightColor.A = 0.15f;
	FCanvasTileItem Highlight(FVector2D(X + BorderThickness * 2, Y + BorderThickness * 2),
							  FVector2D(Size - BorderThickness * 4, Size * 0.3f), HighlightColor);
	Highlight.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Highlight);

	FVector2D KeyTextSize;
	const float KeyTextScale = KeyLabel.Len() > 3 ? UIScale * 1.2f : UIScale * 1.8f;
	Canvas->TextSize(GEngine->GetMediumFont(), KeyLabel, KeyTextSize.X, KeyTextSize.Y, KeyTextScale, KeyTextScale);
	const FVector2D KeyTextPos(X + (Size - KeyTextSize.X) / 2, Y + Size * 0.20f);
	DrawText(KeyLabel, KeyTextPos, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), KeyTextScale);

	TArray<FString> ActionLines;
	ActionLabel.ParseIntoArray(ActionLines, TEXT("\n"), false);

	const float ActionTextScale = UIScale * 0.85f;
	float LineY = Y + Size * 0.62f;

	for (const FString& Line : ActionLines)
	{
		FVector2D ActionTextSize;
		Canvas->TextSize(GEngine->GetMediumFont(), Line, ActionTextSize.X, ActionTextSize.Y, ActionTextScale, ActionTextScale);
		const FVector2D ActionTextPos(X + (Size - ActionTextSize.X) / 2, LineY);
		DrawText(Line, ActionTextPos, Color, ActionTextScale);
		LineY += ActionTextSize.Y + 3.0f * UIScale;
	}
}
