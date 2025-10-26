// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalHUD.h"
#include "FractalPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "CanvasItem.h"

void AFractalHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
		return;

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

	DrawCompactStatBar(X, CurrentY, BarWidth, BarHeight,
					   TEXT("ZOOM"), ZoomLevel, 0.0f, 100.0f, TEXT("x"), FLinearColor(1.0f, 0.6f, 0.2f, 0.9f), UIScale);
	CurrentY += LineSpacing;

	DrawCompactStatBar(X, CurrentY, BarWidth, BarHeight,
					   TEXT("SPEED LIMIT"), SpeedPercent, 0.0f, 100.0f, TEXT("%"), FLinearColor(0.3f, 1.0f, 0.5f, 0.9f), UIScale);
	CurrentY += LineSpacing;

	const float VelMagnitude = CurrentVelocity.Size() * 0.01f;
	const float VelMax = MaxSpeed * 0.01f;
	DrawCompactStatBar(X, CurrentY, BarWidth, BarHeight,
					   TEXT("VELOCITY"), VelMagnitude, 0.0f, VelMax > 0 ? VelMax : 1.0f, TEXT("m/s"),
					   FLinearColor(0.4f, 0.9f, 1.0f, 0.9f), UIScale);
}

void AFractalHUD::DrawTopRightInfo(float X, float Y, float UIScale)
{
	float CurrentY = Y;
	const float LineSpacing = 36.0f * UIScale;

	DrawText(TEXT("TELEMETRY"), FVector2D(X - 320.0f * UIScale, CurrentY),
			 FLinearColor(1.0f, 0.8f, 0.4f, 0.95f), UIScale * 1.3f);
	CurrentY += LineSpacing * 1.2f;

	const float LabelWidth = 180.0f * UIScale;
	const float DistScale = 0.01f;

	DrawText(TEXT("DIST TO FRACTAL"), FVector2D(X - 320.0f * UIScale, CurrentY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.95f), UIScale * 1.1f);
	FString DistText = FString::Printf(TEXT("%.4f m"), Distance * DistScale);
	DrawText(DistText, FVector2D(X - 110.0f * UIScale, CurrentY),
			 FLinearColor(1.0f, 0.9f, 0.3f, 0.95f), UIScale * 1.1f);
	CurrentY += LineSpacing;

	DrawText(TEXT("POSITION"), FVector2D(X - 320.0f * UIScale, CurrentY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.95f), UIScale * 1.1f);
	FString PosText = FString::Printf(TEXT("X:%+.2f Y:%+.2f Z:%+.2f"), LocalPos.X, LocalPos.Y, LocalPos.Z);
	DrawText(PosText, FVector2D(X - 280.0f * UIScale, CurrentY + 20.0f * UIScale),
			 FLinearColor(0.7f, 0.5f, 1.0f, 0.95f), UIScale * 0.95f);
}

void AFractalHUD::DrawBottomLeftInfo(float X, float Y, float UIScale)
{
	const float LineHeight = 28.0f * UIScale;
	float CurrentY = Y - LineHeight;

	FString BuildInfo = TEXT("FRACTAL EXPLORER v1.0");
	DrawText(BuildInfo, FVector2D(X, CurrentY),
			 FLinearColor(0.5f, 0.5f, 0.6f, 0.6f), UIScale * 0.9f);
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
	const FFractalParameterPreset &Preset = CurrentFractalPreset;
	
	// Fractal names - must match order of FRACTAL_TYPE_* defines in shader
	// When adding fractals: Update this array, shader defines, and PlayerController::MaxFractalType
	const TArray<FString> FractalNames = {
		TEXT("Mandelbulb"),
		TEXT("Burning Ship"),
		TEXT("Julia Set"),
		TEXT("Mandelbox"),
		TEXT("Inverted Menger"),
		TEXT("Quaternion"),
		TEXT("Sierpinski Tetrahedron"),
		TEXT("Kaleidoscopic IFS")
	};
	
	// Update transition animation
	if (CurrentFractalType != PreviousFractalType)
	{
		float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		if (LastTransitionTime == 0.0f || CurrentTime - LastTransitionTime > 0.5f)
		{
			LastTransitionTime = CurrentTime;
			TypeTransitionProgress = 0.0f;
		}
	}
	
	if (TypeTransitionProgress < 1.0f)
	{
		float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		float DeltaTime = CurrentTime - LastTransitionTime;
		TypeTransitionProgress = FMath::Clamp(DeltaTime / 0.3f, 0.0f, 1.0f);
		
		if (TypeTransitionProgress >= 1.0f)
		{
			PreviousFractalType = CurrentFractalType;
		}
	}
	
	// Smooth easing function
	float EasedProgress = TypeTransitionProgress * TypeTransitionProgress * (3.0f - 2.0f * TypeTransitionProgress);
	
	// Type wheel in bottom-right corner
	const float TypePanelWidth = 240.0f * UIScale;
	const float TypePanelHeight = 120.0f * UIScale;
	const float TypePanelX = X - TypePanelWidth;
	const float TypePanelY = BottomAlignY - TypePanelHeight;
	const float Padding = 12.0f * UIScale;
	
	DrawPanel(TypePanelX, TypePanelY, TypePanelWidth, TypePanelHeight, FLinearColor(0.01f, 0.01f, 0.03f, 0.75f));
	
	float CurrentY = TypePanelY + Padding;
	
	DrawText(TEXT("TYPE"), FVector2D(TypePanelX + Padding, CurrentY),
			 FLinearColor(0.5f, 0.5f, 0.6f, 0.9f), UIScale * 1.0f);
	CurrentY += 28.0f * UIScale;
	
	const float ListCenterBase = TypePanelY + (TypePanelHeight * 0.5f);
	const float WheelCenterY = ListCenterBase + 8.0f * UIScale;
	const float ItemSpacing = 35.0f * UIScale;
	const float PrevItemSpacing = ItemSpacing * 0.65f;
	const float TransitionOffset = ItemSpacing * EasedProgress;
	
	int32 NumTypes = FractalNames.Num();
	int32 PrevType = (CurrentFractalType - 1 + NumTypes) % NumTypes;
	int32 NextType = (CurrentFractalType + 1) % NumTypes;
	
	// Colors for interpolation
	const FLinearColor FadedColor = FLinearColor(0.3f, 0.3f, 0.4f, 0.4f);
	const FLinearColor BrightColor = FLinearColor(0.3f, 1.0f, 0.5f, 1.0f);
	
	// Scales for interpolation
	const float FadedScale = 0.8f;
	const float BrightScale = 1.1f;
	
	// Draw previous (above, fading out and shrinking)
	float PrevY = WheelCenterY - PrevItemSpacing - TransitionOffset;
	if (PrevY >= TypePanelY - 20.0f * UIScale && PrevY <= TypePanelY + TypePanelHeight)
	{
		// Previous is becoming more faded and smaller as it moves out
		float PrevScale = FMath::Lerp(BrightScale, FadedScale, EasedProgress);
		FLinearColor PrevColor = FLinearColor::LerpUsingHSV(BrightColor, FadedColor, EasedProgress);
		
		DrawText(FractalNames[PrevType], FVector2D(TypePanelX + Padding + 8.0f * UIScale, PrevY),
				 PrevColor, UIScale * PrevScale);
	}
	
	// Draw current (center, transitioning from faded to bright)
	float CurY = WheelCenterY - TransitionOffset;
	if (CurY >= TypePanelY - 20.0f * UIScale && CurY <= TypePanelY + TypePanelHeight + 20.0f * UIScale)
	{
		// Current is growing and brightening as it centers
		float CurScale = FMath::Lerp(FadedScale, BrightScale, EasedProgress);
		FLinearColor CurColor = FLinearColor::LerpUsingHSV(FadedColor, BrightColor, EasedProgress);
		
		DrawText(FractalNames[CurrentFractalType], FVector2D(TypePanelX + Padding + 8.0f * UIScale, CurY),
				 CurColor, UIScale * CurScale);
	}
	
	// Draw next (below, transitioning from faded to current position)
	float NextY = WheelCenterY + ItemSpacing - TransitionOffset;
	if (NextY >= TypePanelY && NextY <= TypePanelY + TypePanelHeight + 20.0f * UIScale)
	{
		// Next remains faded and small as it moves up toward center
		float NextScale = FadedScale;
		FLinearColor NextColor = FadedColor;
		
		DrawText(FractalNames[NextType], FVector2D(TypePanelX + Padding + 8.0f * UIScale, NextY),
				 NextColor, UIScale * NextScale);
	}
	
	// Hints and version centered along bottom alignment
	if (!bShowHelp)
	{
		const float HintSpacing = 8.0f * UIScale;
		FVector2D TitleSize(0.0f, 0.0f);
		FVector2D HintSize(0.0f, 0.0f);
		Canvas->TextSize(GEngine->GetMediumFont(), TEXT("FRACTAL EXPLORER v1.0"), TitleSize.X, TitleSize.Y, UIScale * 0.85f, UIScale * 0.85f);
		Canvas->TextSize(GEngine->GetMediumFont(), TEXT("[ H ] CONTROLS  •  [ R ] RESET"), HintSize.X, HintSize.Y, UIScale * 0.9f, UIScale * 0.9f);
		const float HintTopY = BottomAlignY - (TitleSize.Y + HintSpacing + HintSize.Y);
		
		DrawCenteredText(TEXT("FRACTAL EXPLORER v1.0"),
			FVector2D(ScreenWidth / 2, HintTopY),
			FLinearColor(0.4f, 0.4f, 0.5f, 0.5f), UIScale * 0.85f);
		
		DrawCenteredText(TEXT("[ H ] CONTROLS  •  [ R ] RESET"),
			FVector2D(ScreenWidth / 2, HintTopY + TitleSize.Y + HintSpacing),
			FLinearColor(0.5f, 0.7f, 0.9f, 0.6f), UIScale * 0.9f);
	}

	// Power and scale bars in bottom-left corner (mirrors top-left stats)
	const float StatX = Margin;
	float StatY = BottomAlignY - StatLineSpacing - StatBarHeight;

	DrawCompactStatBar(StatX, StatY, StatBarWidth, StatBarHeight,
		TEXT("POWER"), CurrentPower, Preset.MinPower, Preset.MaxPower, TEXT(""),
		FLinearColor(1.0f, 0.6f, 0.2f, 0.9f), UIScale, 1);

	const float ScaleDisplay = CurrentScaleMultiplier * 1000.0f;
	const float ScaleMinDisplay = Preset.MinScale * 1000.0f;
	const float ScaleMaxDisplay = Preset.MaxScale * 1000.0f;
	DrawCompactStatBar(StatX, StatY + StatLineSpacing, StatBarWidth, StatBarHeight,
		TEXT("SCALE"), ScaleDisplay, ScaleMinDisplay, ScaleMaxDisplay, TEXT("x10^-3"),
		FLinearColor(0.3f, 0.9f, 1.0f, 0.9f), UIScale, 2);
}

void AFractalHUD::DrawInfoPanel(float X, float Y, float UIScale)
{
	DrawTopLeftInfo(X, Y, UIScale);
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

	FLinearColor BackColor = FLinearColor(0.05f, 0.05f, 0.1f, 0.6f);
	FCanvasTileItem Background(FVector2D(SpacebarX, Y), FVector2D(SpacebarWidth, KeySize), BackColor);
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	FLinearColor HighlightColor = VertColor;
	HighlightColor.A = 0.2f;
	FCanvasTileItem Highlight(FVector2D(SpacebarX, Y), FVector2D(SpacebarWidth, KeySize * 0.35f), HighlightColor);
	Highlight.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Highlight);

	DrawBox(SpacebarX, Y, SpacebarWidth, KeySize, 2.0f, VertColor);

	FVector2D KeyTextSize;
	float KeyTextScale = UIScale * 1.8f;
	Canvas->TextSize(GEngine->GetMediumFont(), TEXT("SPACE"), KeyTextSize.X, KeyTextSize.Y, KeyTextScale, KeyTextScale);
	FVector2D KeyTextPos(SpacebarX + (SpacebarWidth - KeyTextSize.X) / 2, Y + KeySize * 0.20f);
	DrawText(TEXT("SPACE"), KeyTextPos, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), KeyTextScale);

	float ActionTextScale = UIScale * 0.85f;
	FVector2D ActionTextSize;
	Canvas->TextSize(GEngine->GetMediumFont(), TEXT("ASCEND"), ActionTextSize.X, ActionTextSize.Y, ActionTextScale, ActionTextScale);
	FVector2D ActionTextPos(SpacebarX + (SpacebarWidth - ActionTextSize.X) / 2, Y + KeySize * 0.62f);
	DrawText(TEXT("ASCEND"), ActionTextPos, VertColor, ActionTextScale);
}

void AFractalHUD::DrawMouseControls(float X, float Y, float UIScale)
{
	const FLinearColor MouseColor = FLinearColor(1.0f, 0.6f, 0.9f, 1.0f);

	const float WheelWidth = 50.0f * UIScale;
	const float WheelHeight = 70.0f * UIScale;

	DrawText(TEXT("SCROLL WHEEL"), FVector2D(X + WheelWidth / 2 - 60.0f * UIScale, Y - 25.0f * UIScale),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 1.1f);

	DrawMouseWheel(X, Y, WheelWidth, WheelHeight, MouseColor, UIScale);

	DrawText(TEXT("Speed Limit"), FVector2D(X + WheelWidth / 2 - 48.0f * UIScale, Y + WheelHeight + 12.0f * UIScale),
			 MouseColor, UIScale * 0.85f);
	
	float ButtonY = Y + WheelHeight + 45.0f * UIScale;
	
	DrawText(TEXT("LEFT CLICK"), FVector2D(X + WheelWidth / 2 - 45.0f * UIScale, ButtonY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 0.95f);
	DrawText(TEXT("Decrease Power"), FVector2D(X + WheelWidth / 2 - 60.0f * UIScale, ButtonY + 18.0f * UIScale),
			 MouseColor, UIScale * 0.75f);
	
	ButtonY += 42.0f * UIScale;
	
	DrawText(TEXT("RIGHT CLICK"), FVector2D(X + WheelWidth / 2 - 50.0f * UIScale, ButtonY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 0.95f);
	DrawText(TEXT("Increase Power"), FVector2D(X + WheelWidth / 2 - 60.0f * UIScale, ButtonY + 18.0f * UIScale),
			 MouseColor, UIScale * 0.75f);
}

void AFractalHUD::DrawMouseButtons(float X, float Y, float UIScale)
{
	const FLinearColor MouseColor = FLinearColor(1.0f, 0.6f, 0.9f, 1.0f);
	const FLinearColor ScaleColor = FLinearColor(0.3f, 0.9f, 1.0f, 1.0f);
	
	float ButtonY = Y;
	
	DrawText(TEXT("LEFT CLICK"), FVector2D(X - 45.0f * UIScale, ButtonY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 0.95f);
	DrawText(TEXT("Decrease Power"), FVector2D(X - 60.0f * UIScale, ButtonY + 18.0f * UIScale),
			 MouseColor, UIScale * 0.75f);
	
	ButtonY += 42.0f * UIScale;
	
	DrawText(TEXT("RIGHT CLICK"), FVector2D(X - 50.0f * UIScale, ButtonY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 0.95f);
	DrawText(TEXT("Increase Power"), FVector2D(X - 60.0f * UIScale, ButtonY + 18.0f * UIScale),
			 MouseColor, UIScale * 0.75f);
	
	ButtonY += 42.0f * UIScale;
	
	DrawText(TEXT("MOUSE BACK"), FVector2D(X - 50.0f * UIScale, ButtonY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 0.95f);
	DrawText(TEXT("Decrease Scale"), FVector2D(X - 60.0f * UIScale, ButtonY + 18.0f * UIScale),
			 ScaleColor, UIScale * 0.75f);
	
	ButtonY += 42.0f * UIScale;
	
	DrawText(TEXT("MOUSE FORWARD"), FVector2D(X - 60.0f * UIScale, ButtonY),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.9f), UIScale * 0.95f);
	DrawText(TEXT("Increase Scale"), FVector2D(X - 60.0f * UIScale, ButtonY + 18.0f * UIScale),
			 ScaleColor, UIScale * 0.75f);
}

void AFractalHUD::DrawMouseWheel(float X, float Y, float Width, float Height, const FLinearColor &Color, float UIScale)
{
	const FLinearColor BackColor = FLinearColor(0.05f, 0.05f, 0.1f, 0.6f);
	const FLinearColor BorderColor = Color;
	const float Radius = Width / 2.0f;

	const float CenterX = X + Width / 2.0f;
	const float TopCapY = Y + Radius;
	const float BottomCapY = Y + Height - Radius;

	for (float dy = 0; dy < Height; dy += 1.0f)
	{
		for (float dx = 0; dx < Width; dx += 2.0f)
		{
			float px = X + dx;
			float py = Y + dy;

			bool inside = false;
			if (py >= TopCapY && py <= BottomCapY)
			{
				inside = true;
			}
			else if (py < TopCapY)
			{
				float distToCenter = FMath::Sqrt(FMath::Square(px - CenterX) + FMath::Square(py - TopCapY));
				inside = (distToCenter <= Radius);
			}
			else if (py > BottomCapY)
			{
				float distToCenter = FMath::Sqrt(FMath::Square(px - CenterX) + FMath::Square(py - BottomCapY));
				inside = (distToCenter <= Radius);
			}

			if (inside)
			{
				FCanvasTileItem Pixel(FVector2D(px, py), FVector2D(2.0f, 1.0f), BackColor);
				Pixel.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Pixel);
			}
		}
	}

	FLinearColor HighlightColor = Color;
	HighlightColor.A = 0.2f;
	const float HighlightHeight = Height * 0.35f;
	for (float dy = 0; dy < HighlightHeight; dy += 1.0f)
	{
		for (float dx = 0; dx < Width; dx += 2.0f)
		{
			float px = X + dx;
			float py = Y + dy;

			bool inside = false;
			if (py >= TopCapY && py <= BottomCapY)
			{
				inside = true;
			}
			else if (py < TopCapY)
			{
				float distToCenter = FMath::Sqrt(FMath::Square(px - CenterX) + FMath::Square(py - TopCapY));
				inside = (distToCenter <= Radius);
			}

			if (inside)
			{
				FCanvasTileItem Pixel(FVector2D(px, py), FVector2D(2.0f, 1.0f), HighlightColor);
				Pixel.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Pixel);
			}
		}
	}

	const float BorderThickness = 2.0f;
	for (float dy = 0; dy < Height; dy += 1.0f)
	{
		for (float dx = 0; dx < Width; dx += 1.0f)
		{
			float px = X + dx;
			float py = Y + dy;

			float distFromEdge = 999.0f;

			if (py >= TopCapY && py <= BottomCapY)
			{
				distFromEdge = FMath::Min(FMath::Abs(dx), FMath::Abs(dx - Width));
			}
			else if (py < TopCapY)
			{
				float distToCenter = FMath::Sqrt(FMath::Square(px - CenterX) + FMath::Square(py - TopCapY));
				distFromEdge = FMath::Abs(distToCenter - Radius);
			}
			else if (py > BottomCapY)
			{
				float distToCenter = FMath::Sqrt(FMath::Square(px - CenterX) + FMath::Square(py - BottomCapY));
				distFromEdge = FMath::Abs(distToCenter - Radius);
			}

			if (distFromEdge <= BorderThickness)
			{
				FCanvasTileItem BorderPixel(FVector2D(px, py), FVector2D(1.0f, 1.0f), BorderColor);
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

void AFractalHUD::DrawPanel(float X, float Y, float Width, float Height, const FLinearColor &Color)
{
	FCanvasTileItem BackPanel(FVector2D(X, Y), FVector2D(Width, Height), Color);
	BackPanel.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(BackPanel);

	const float BorderThickness = 3.0f;
	FLinearColor BorderColor = FLinearColor(0.3f, 0.7f, 1.0f, 0.8f);

	DrawBox(X, Y, Width, Height, BorderThickness, BorderColor);

	const float InnerGlow = 8.0f;
	FLinearColor GlowColor = FLinearColor(0.2f, 0.5f, 0.8f, 0.2f);
	DrawBox(X + BorderThickness, Y + BorderThickness,
			Width - BorderThickness * 2, Height - BorderThickness * 2, InnerGlow, GlowColor);
}

void AFractalHUD::DrawBox(float X, float Y, float Width, float Height, float Thickness, const FLinearColor &Color)
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

void AFractalHUD::DrawCompactStatBar(float X, float Y, float Width, float Height,
							 const FString &Label, float Value, float MinValue, float MaxValue, const FString &Unit, const FLinearColor &BarColor, float UIScale, int32 DecimalPlaces)
{
	const float LabelY = Y - 22.0f * UIScale;
	DrawText(Label, FVector2D(X, LabelY),
			 FLinearColor(0.8f, 0.8f, 0.9f, 0.95f), UIScale * 1.1f);

	FLinearColor BackColor = FLinearColor(0.05f, 0.05f, 0.1f, 0.6f);
	FCanvasTileItem Background(FVector2D(X, Y), FVector2D(Width, Height), BackColor);
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	const float ClampedValue = FMath::Clamp(Value, MinValue, MaxValue);
	const float SafeRange = FMath::Max(MaxValue - MinValue, KINDA_SMALL_NUMBER);
	const float Percentage = FMath::Clamp((ClampedValue - MinValue) / SafeRange, 0.0f, 1.0f);
	const float FilledWidth = Width * Percentage;

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

	const int32 Precision = FMath::Max(DecimalPlaces, 0);
	FString NumericText = FString::Printf(TEXT("%.*f"), Precision, ClampedValue);
	FString ValueText = Unit.IsEmpty() ? NumericText : FString::Printf(TEXT("%s %s"), *NumericText, *Unit);
	const float ValueX = X + Width + 12.0f * UIScale;
	DrawText(ValueText, FVector2D(ValueX, Y - 6.0f * UIScale),
			 BarColor, UIScale * 1.0f);
}

void AFractalHUD::DrawRightAlignedMetric(float X, float Y, const FString &Label,
										 float Value, const FString &Unit, const FLinearColor &Color, float UIScale)
{
	FString ValueText = FString::Printf(TEXT("%.4f %s"), Value, *Unit);

	FVector2D TextSize;
	Canvas->TextSize(GEngine->GetMediumFont(), ValueText, TextSize.X, TextSize.Y, UIScale * 1.1f, UIScale * 1.1f);

	DrawText(Label, FVector2D(X - TextSize.X - 100.0f * UIScale, Y),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.95f), UIScale * 1.1f);
	DrawText(ValueText, FVector2D(X - TextSize.X, Y), Color, UIScale * 1.1f);
}

void AFractalHUD::DrawRightAlignedPosition(float X, float Y, float UIScale)
{
	FString PosText = FString::Printf(TEXT("X:%+.3f  Y:%+.3f  Z:%+.3f"), LocalPos.X, LocalPos.Y, LocalPos.Z);

	FVector2D TextSize;
	Canvas->TextSize(GEngine->GetMediumFont(), PosText, TextSize.X, TextSize.Y, UIScale * 1.0f, UIScale * 1.0f);

	DrawText(TEXT("POSITION"), FVector2D(X - TextSize.X - 100.0f * UIScale, Y),
			 FLinearColor(0.7f, 0.7f, 0.8f, 0.95f), UIScale * 1.1f);
	DrawText(PosText, FVector2D(X - TextSize.X, Y),
			 FLinearColor(0.7f, 0.5f, 1.0f, 0.95f), UIScale * 1.0f);
}

void AFractalHUD::DrawRightAlignedText(const FString &Text, const FVector2D &Position,
									   const FLinearColor &Color, float Scale)
{
	FVector2D TextSize;
	Canvas->TextSize(GEngine->GetMediumFont(), Text, TextSize.X, TextSize.Y, Scale, Scale);

	DrawText(Text, FVector2D(Position.X - TextSize.X, Position.Y), Color, Scale);
}

void AFractalHUD::DrawStatBar(float X, float Y, float Width, float Height,
							  const FString &Label, float Value, float MaxValue, const FLinearColor &BarColor, float UIScale, bool ShowValue)
{
	DrawCompactStatBar(X, Y, Width, Height, Label, Value, 0.0f, MaxValue, TEXT("%"), BarColor, UIScale);
}

void AFractalHUD::DrawGradientBar(float X, float Y, float Width, float Height,
								  const FLinearColor &StartColor, const FLinearColor &EndColor)
{
	const int32 Steps = 20;
	const float StepWidth = Width / Steps;

	for (int32 i = 0; i < Steps; ++i)
	{
		float Alpha = (float)i / (float)Steps;
		FLinearColor Color = FMath::Lerp(StartColor, EndColor, Alpha);

		FCanvasTileItem Step(FVector2D(X + i * StepWidth, Y), FVector2D(StepWidth + 1, Height), Color);
		Step.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Step);
	}
}

void AFractalHUD::DrawMetricDisplay(float X, float Y, float Width, const FString &Label,
									float Value, const FString &Unit, const FLinearColor &Color, float UIScale)
{
	DrawTextWithShadow(Label, FVector2D(X, Y), FLinearColor(0.7f, 0.7f, 0.7f), UIScale * 0.85f);

	FString ValueText = FString::Printf(TEXT("%.4f %s"), Value, *Unit);
	DrawTextWithShadow(ValueText, FVector2D(X + 100.0f * UIScale, Y), Color, UIScale * 0.9f);
}

void AFractalHUD::DrawPositionDisplay(float X, float Y, float Width, float UIScale)
{
	DrawTextWithShadow(TEXT("POSITION"), FVector2D(X, Y), FLinearColor(0.7f, 0.7f, 0.7f), UIScale * 0.85f);

	FString PosText = FString::Printf(TEXT("X:%+.3f Y:%+.3f Z:%+.3f"), LocalPos.X, LocalPos.Y, LocalPos.Z);
	DrawTextWithShadow(PosText, FVector2D(X, Y + 16.0f * UIScale),
					   FLinearColor(0.8f, 0.6f, 0.9f), UIScale * 0.75f);
}

void AFractalHUD::DrawText(const FString &Text, const FVector2D &Position,
						   const FLinearColor &Color, float Scale)
{
	FCanvasTextItem TextItem(Position, FText::FromString(Text), GEngine->GetMediumFont(), Color);
	TextItem.Scale = FVector2D(Scale, Scale);
	TextItem.bOutlined = false;
	TextItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(TextItem);
}

void AFractalHUD::DrawTextWithShadow(const FString &Text, const FVector2D &Position,
									 const FLinearColor &Color, float Scale)
{
	FVector2D ShadowOffset(2.5f * Scale, 2.5f * Scale);
	FLinearColor ShadowColor = FLinearColor(0.0f, 0.0f, 0.0f, Color.A * 0.9f);

	FCanvasTextItem ShadowText(Position + ShadowOffset, FText::FromString(Text), GEngine->GetMediumFont(), ShadowColor);
	ShadowText.Scale = FVector2D(Scale, Scale);
	ShadowText.EnableShadow(ShadowColor);
	Canvas->DrawItem(ShadowText);

	FCanvasTextItem TextItem(Position, FText::FromString(Text), GEngine->GetMediumFont(), Color);
	TextItem.Scale = FVector2D(Scale, Scale);
	Canvas->DrawItem(TextItem);
}

void AFractalHUD::DrawCenteredText(const FString &Text, const FVector2D &Position,
								   const FLinearColor &Color, float Scale)
{
	FCanvasTextItem TextItem(Position, FText::FromString(Text), GEngine->GetMediumFont(), Color);
	TextItem.Scale = FVector2D(Scale, Scale);
	TextItem.bCentreX = true;
	TextItem.bOutlined = false;
	TextItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(TextItem);
}

void AFractalHUD::DrawKeyGraphic(float X, float Y, const FString &Keys, const FLinearColor &Color, float UIScale)
{
	TArray<FString> KeyArray;
	Keys.ParseIntoArray(KeyArray, TEXT(" "), true);

	const float KeySize = 32.0f * UIScale;
	const float KeySpacing = 8.0f * UIScale;
	const float TotalWidth = (KeySize * KeyArray.Num()) + (KeySpacing * (KeyArray.Num() - 1));

	float CurrentX = X - TotalWidth;

	for (const FString &Key : KeyArray)
	{
		DrawKeyButton(CurrentX, Y - 8.0f * UIScale, KeySize, KeySize, Key, Color, UIScale);
		CurrentX += KeySize + KeySpacing;
	}
}

void AFractalHUD::DrawKeyButton(float X, float Y, float Width, float Height, const FString &Label, const FLinearColor &Color, float UIScale)
{
	const float Radius = 4.0f * UIScale;

	FLinearColor BgColor = FLinearColor(0.08f, 0.08f, 0.12f, 0.9f);
	FCanvasTileItem Background(FVector2D(X, Y), FVector2D(Width, Height), BgColor);
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	const float BorderThickness = 2.0f * UIScale;
	FLinearColor BorderColor = Color;
	BorderColor.A = 0.8f;
	DrawBox(X, Y, Width, Height, BorderThickness, BorderColor);

	FLinearColor InnerBorderColor = Color;
	InnerBorderColor.A = 0.3f;
	DrawBox(X + BorderThickness, Y + BorderThickness,
			Width - BorderThickness * 2, Height - BorderThickness * 2, 1.0f * UIScale, InnerBorderColor);

	FVector2D TextSize;
	float TextScale = UIScale * 0.9f;
	if (Label.Len() > 3)
	{
		TextScale = UIScale * 0.65f;
	}

	Canvas->TextSize(GEngine->GetMediumFont(), Label, TextSize.X, TextSize.Y, TextScale, TextScale);
	FVector2D TextPos(X + (Width - TextSize.X) / 2, Y + (Height - TextSize.Y) / 2);

	DrawText(Label, TextPos, Color, TextScale);
}

void AFractalHUD::DrawLabeledKey(float X, float Y, float Size, const FString &KeyLabel, const FString &ActionLabel, const FLinearColor &Color, float UIScale)
{
	FLinearColor BgColor = FLinearColor(0.08f, 0.08f, 0.12f, 0.9f);
	FCanvasTileItem Background(FVector2D(X, Y), FVector2D(Size, Size), BgColor);
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
	float KeyTextScale = UIScale * 1.8f;
	if (KeyLabel.Len() > 3)
	{
		KeyTextScale = UIScale * 1.2f;
	}
	Canvas->TextSize(GEngine->GetMediumFont(), KeyLabel, KeyTextSize.X, KeyTextSize.Y, KeyTextScale, KeyTextScale);
	FVector2D KeyTextPos(X + (Size - KeyTextSize.X) / 2, Y + Size * 0.20f);
	DrawText(KeyLabel, KeyTextPos, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), KeyTextScale);

	TArray<FString> ActionLines;
	ActionLabel.ParseIntoArray(ActionLines, TEXT("\n"), false);

	float ActionTextScale = UIScale * 0.85f;
	float LineY = Y + Size * 0.62f;

	for (const FString &Line : ActionLines)
	{
		FVector2D ActionTextSize;
		Canvas->TextSize(GEngine->GetMediumFont(), Line, ActionTextSize.X, ActionTextSize.Y, ActionTextScale, ActionTextScale);
		FVector2D ActionTextPos(X + (Size - ActionTextSize.X) / 2, LineY);
		DrawText(Line, ActionTextPos, Color, ActionTextScale);
		LineY += ActionTextSize.Y + 3.0f * UIScale;
	}
}
