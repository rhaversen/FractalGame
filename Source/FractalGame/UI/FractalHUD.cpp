#include "FractalHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"

void AFractalHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	APawn* OwningPawn = GetOwningPawn();
	if (!OwningPawn)
	{
		return;
	}

	const FVector Velocity = OwningPawn->GetVelocity();
	const float SpeedMetersPerSecond = Velocity.Size() * 0.01f;

	const FVector LocationCentimeters = OwningPawn->GetActorLocation();
	const FVector LocationMeters = LocationCentimeters * 0.01f;

	const float ReferenceHeight = 1080.0f;
	const float ScaleFactor = Canvas->SizeY / ReferenceHeight;
	const float UIScale = FMath::Clamp(ScaleFactor, 0.75f, 1.5f);
	const float Margin = 24.0f * UIScale;
	const float LineSpacing = 34.0f * UIScale;

	const FString SpeedText = FString::Printf(TEXT("%.1f m/s"), SpeedMetersPerSecond);
	const FString PositionText = FString::Printf(TEXT("X %.2f m\nY %.2f m\nZ %.2f m"), LocationMeters.X, LocationMeters.Y, LocationMeters.Z);

	const auto DrawLine = [&](const FString& Text, float Y)
	{
		FCanvasTextItem TextItem(FVector2D(Margin, Y), FText::FromString(Text), GEngine->GetMediumFont(), FLinearColor::White);
		TextItem.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		TextItem.Scale = FVector2D(UIScale, UIScale);
		Canvas->DrawItem(TextItem);
	};

	DrawLine(SpeedText, Margin);

	float CurrentY = Margin + LineSpacing;
	TArray<FString> PositionLines;
	PositionText.ParseIntoArrayLines(PositionLines);
	for (const FString& Line : PositionLines)
	{
		DrawLine(Line, CurrentY);
		CurrentY += LineSpacing;
	}
}
