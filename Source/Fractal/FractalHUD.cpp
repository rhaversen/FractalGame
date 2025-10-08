// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalHUD.h"
#include "FractalPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "CanvasItem.h"

void AFractalHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas) return;

	// Calculate scale factor based on screen height (1080p as reference)
	const float ReferenceHeight = 1080.0f;
	const float ScaleFactor = Canvas->SizeY / ReferenceHeight;
	const float UIScale = FMath::Clamp(ScaleFactor, 0.5f, 2.0f);

	// Scaled margins and sizes
	const float MarginX = 20.0f * UIScale;
	const float MarginY = 20.0f * UIScale;
	const float TextureSize = 200.0f * UIScale;

	// Optional: Draw textures if set (for future use)
	if (OverlayTexture)
	{
		FCanvasTileItem TileItem(
			FVector2D(Canvas->SizeX - TextureSize - MarginX, MarginY),
			OverlayTexture->GetResource(),
			FVector2D(TextureSize, TextureSize),
			FLinearColor::White
		);
		TileItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(TileItem);
	}

	if (RenderTarget)
	{
		FCanvasTileItem RTItem(
			FVector2D(MarginX, Canvas->SizeY - TextureSize - MarginY),
			RenderTarget->GetResource(),
			FVector2D(TextureSize, TextureSize),
			FLinearColor::White
		);
		Canvas->DrawItem(RTItem);
	}

	if (HUDMaterial)
	{
		const float MatWidth = 200.0f * UIScale;
		const float MatHeight = 100.0f * UIScale;
		FCanvasTileItem MatItem(
			FVector2D(Canvas->SizeX / 2 - MatWidth / 2, MarginY),
			FVector2D(MatWidth, MatHeight),
			FLinearColor::White
		);
		MatItem.MaterialRenderProxy = HUDMaterial->GetRenderProxy();
		Canvas->DrawItem(MatItem);
	}

	// Draw debug text with scaled positioning and font
	const float StartY = 50.0f * UIScale;
	float CurrentY = StartY;
	const float LineHeight = 25.0f * UIScale;
	const FVector2D TextScale(1.2f * UIScale, 1.2f * UIScale);

	auto DrawLine = [&](const FString& Text, const FLinearColor& Color)
	{
		FCanvasTextItem TextItem(FVector2D(MarginX, CurrentY), FText::FromString(Text), GEngine->GetLargeFont(), Color);
		TextItem.Scale = TextScale;
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);
		CurrentY += LineHeight;
	};

	if (bShowDebug)
	{
		DrawLine(FString::Printf(TEXT("Zoom  %s%.0f%%"), *CreateBar(ZoomLevel), ZoomLevel), FLinearColor(1.0f, 0.59f, 0.2f));
		DrawLine(FString::Printf(TEXT("Speed %s%.0f%%"), *CreateBar(SpeedPercent), SpeedPercent), FLinearColor(0.0f, 1.0f, 0.59f));
		
		const float VelMagnitude = CurrentVelocity.Size();
		const float VelScale = 0.01f;
		DrawLine(FString::Printf(TEXT("Vel   %.3f m/s"), VelMagnitude * VelScale), FLinearColor(0.2f, 1.0f, 1.0f));
		
		const float MaxSpeedScale = 0.01f;
		DrawLine(FString::Printf(TEXT("Max   %.3f m/s"), MaxSpeed * MaxSpeedScale), FLinearColor(1.0f, 0.78f, 0.2f));
		
		const float DistScale = 0.01f;
		DrawLine(FString::Printf(TEXT("Dist  %.4f m"), Distance * DistScale), FLinearColor(1.0f, 1.0f, 0.2f));
		
		DrawLine(FString::Printf(TEXT("Pos   X:%+.4f Y:%+.4f Z:%+.4f"), LocalPos.X, LocalPos.Y, LocalPos.Z), FLinearColor(0.78f, 0.59f, 0.78f));
	}

	if (bShowHelp)
	{
		CurrentY += LineHeight * 0.5f;
		DrawLine(TEXT("Controls"), FLinearColor(1.0f, 0.86f, 0.39f));
		DrawLine(TEXT("Move: W, A, S, D"), FLinearColor(0.59f, 1.0f, 0.78f));
		DrawLine(TEXT("Vertical: Space, Shift"), FLinearColor(0.59f, 1.0f, 0.78f));
		DrawLine(TEXT("Roll: Q, E"), FLinearColor(0.59f, 1.0f, 0.78f));
		DrawLine(TEXT("Speed: Mouse Wheel"), FLinearColor(0.59f, 1.0f, 0.78f));
		DrawLine(TEXT("Reset view: R"), FLinearColor(0.59f, 1.0f, 0.78f));
	}
	else
	{
		DrawLine(TEXT("(Hold H for controls)"), FLinearColor(0.55f, 0.55f, 0.55f));
	}
}

FString AFractalHUD::CreateBar(float Percent, int32 Width) const
{
	int32 Filled = FMath::RoundToInt((Percent / 100.f) * Width);
	Filled = FMath::Clamp(Filled, 0, Width);
	FString S;
	for (int32 i = 0; i < Filled; ++i) { S += TEXT("#"); }
	S += TEXT(" ");
	return S;
}
