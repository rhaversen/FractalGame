#include "FractalControlSubsystem.h"
#include "FractalRenderer.h"
#include "FractalSceneViewExtension.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

// Targeted using-declarations (not a using-directive: Unreal's unity builds share one translation unit).
using FractalMath::FDD;
using FractalMath::FDDVec3;
using FractalMath::FDVec3;

namespace
{
	TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> GetFractalViewExtension()
	{
		if (FFractalRendererModule* Module = FModuleManager::GetModulePtr<FFractalRendererModule>("FractalRenderer"))
		{
			return Module->GetSceneViewExtension();
		}
		return TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe>();
	}
}

void UFractalControlSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const FractalMath::FFractalPreset& Preset = GetPreset(FractalParameters.FractalType);
	FractalParameters.FractalPower = Preset.DefaultPower;
	UserScale = Preset.DefaultScale;
	CameraMapping = FFractalCameraMapping();
	CameraMapping.Scale = UserScale;
	PushParameters();
	PushCameraMapping();
}

void UFractalControlSubsystem::SetFractalParameters(const FFractalParameter& InParams)
{
	FractalParameters = InParams;
	PushParameters();
}

void UFractalControlSubsystem::SetEnabled(bool bInEnabled)
{
	FractalParameters.bEnabled = bInEnabled;
	PushParameters();
}

void UFractalControlSubsystem::SetFractalType(EFractalType InType)
{
	if (InType == EFractalType::Count)
	{
		return;
	}
	// The reference manager notices the new formula and rebuilds the reference before the next frame.
	FractalParameters.FractalType = InType;
	PushParameters();
}

void UFractalControlSubsystem::SetFractalPower(float InPower)
{
	FractalParameters.FractalPower = InPower;
	PushParameters();
}

const FractalMath::FFractalPreset& UFractalControlSubsystem::GetPreset(EFractalType Type)
{
	return FractalMath::GetFractalPreset(ToFractalFormula(Type));
}

FString UFractalControlSubsystem::GetFractalDisplayName(EFractalType Type)
{
	return FString(UTF8_TO_TCHAR(FractalMath::GetFractalName(ToFractalFormula(Type))));
}

void UFractalControlSubsystem::SetUserScale(double InUserScale)
{
	if (!(InUserScale > 0.0) || !FMath::IsFinite(InUserScale) || InUserScale == UserScale)
	{
		return;
	}
	// Scale the camera's fractal position about the fractal origin (the camera stays where it is in the world).
	const double Factor = InUserScale / UserScale;
	const FVector3d Camera = GetCameraWorldLocation();
	const FDDVec3 CameraFractal = CameraMapping.WorldToFractal(Camera);
	CameraMapping.Origin = FDDVec3(CameraFractal.X * Factor, CameraFractal.Y * Factor, CameraFractal.Z * Factor);
	CameraMapping.Anchor = Camera;
	CameraMapping.Scale = FMath::Max(CameraMapping.Scale * Factor, MinFractalScale);
	UserScale = InUserScale;
	PushCameraMapping();
}

double UFractalControlSubsystem::ZoomAroundCamera(double Factor)
{
	if (!(Factor > 0.0) || !FMath::IsFinite(Factor))
	{
		return 1.0;
	}
	const double NewScale = FMath::Clamp(CameraMapping.Scale * Factor, MinFractalScale, UserScale);
	const double Applied = NewScale / CameraMapping.Scale;
	if (Applied == 1.0)
	{
		return 1.0;
	}
	CameraMapping.ZoomAround(GetCameraWorldLocation(), Applied);
	PushCameraMapping();
	return Applied;
}

void UFractalControlSubsystem::ResetView(FVector FractalOriginWorld)
{
	CameraMapping = FFractalCameraMapping();
	CameraMapping.Anchor = FractalOriginWorld;
	CameraMapping.Scale = UserScale;
	PushCameraMapping();
}

FFractalDistanceInfo UFractalControlSubsystem::GetDistanceAtWorld(FVector WorldPosition) const
{
	const FractalMath::EFractalFormula Formula = ToFractalFormula(FractalParameters.FractalType);
	FractalMath::FFormulaParams Params;
	Params.Power = FractalParameters.FractalPower;
	Params.Bailout = FractalParameters.BailoutRadius;
	// Enough iterations to resolve distances down to about one world unit.
	Params.MaxIterations = ComputeIterationBudget(Formula, Params.Power, FractalParameters.MaxIterations, Params.Bailout, CameraMapping.Scale);

	const FDDVec3 Position = CameraMapping.WorldToFractal(WorldPosition);
	// Double resolves world-unit distances while a world unit is >= 1e-9 fractal units; deeper needs double-double.
	const FractalMath::FDistanceEstimate DE = CameraMapping.Scale > 1.0e-9
		? FractalMath::DistanceEstimate(Formula, FractalMath::ToDoubleVec(Position), Params)
		: FractalMath::DistanceEstimate(Formula, Position, Params);

	FFractalDistanceInfo Info;
	Info.FractalDistance = DE.Distance;
	Info.WorldDistance = DE.Distance / CameraMapping.Scale;
	Info.bEscaped = DE.bEscaped;
	return Info;
}

FVector UFractalControlSubsystem::GetCameraFractalPosition() const
{
	const FDVec3 P = FractalMath::ToDoubleVec(GetCameraFractalPositionDD());
	return FVector(P.X, P.Y, P.Z);
}

FDDVec3 UFractalControlSubsystem::GetCameraFractalPositionDD() const
{
	return CameraMapping.WorldToFractal(GetCameraWorldLocation());
}

FString UFractalControlSubsystem::FormatCoordinate(const FDD& Value, int32 Decimals)
{
	Decimals = FMath::Clamp(Decimals, 0, 30);
	const bool bNegative = Value.Hi < 0.0;
	FDD A = bNegative ? -Value : Value;

	// Integer part, then the fraction digit by digit (exact in double-double: each step only multiplies by 10).
	double IntPart = FMath::FloorToDouble(A.Hi);
	FDD Fraction = A - FDD(IntPart);
	if (Fraction.Hi < 0.0)
	{
		IntPart -= 1.0;
		Fraction = Fraction + FDD(1.0);
	}
	TArray<int32> Digits;
	Digits.Reserve(Decimals + 1);
	for (int32 I = 0; I <= Decimals; ++I)
	{
		Fraction = Fraction * 10.0;
		int32 Digit = FMath::Clamp(static_cast<int32>(FMath::FloorToDouble(Fraction.Hi)), 0, 9);
		Fraction = Fraction - FDD(static_cast<double>(Digit));
		if (Fraction.Hi < 0.0 && Digit > 0)
		{
			--Digit;
			Fraction = Fraction + FDD(1.0);
		}
		Digits.Add(Digit);
	}

	// Round half up on the extra digit.
	bool bCarry = Digits.Last() >= 5;
	Digits.Pop();
	for (int32 I = Digits.Num() - 1; I >= 0 && bCarry; --I)
	{
		Digits[I] += 1;
		bCarry = Digits[I] == 10;
		if (bCarry)
		{
			Digits[I] = 0;
		}
	}
	if (bCarry)
	{
		IntPart += 1.0;
	}

	FString Result = FString::Printf(TEXT("%s%.0f"), bNegative ? TEXT("-") : TEXT(""), IntPart);
	if (Decimals > 0)
	{
		Result += TEXT(".");
		for (const int32 Digit : Digits)
		{
			Result.AppendChar(static_cast<TCHAR>(TEXT('0') + Digit));
		}
	}
	return Result;
}

FVector3d UFractalControlSubsystem::GetCameraWorldLocation() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const APlayerController* Controller = GameInstance->GetFirstLocalPlayerController())
		{
			if (Controller->PlayerCameraManager)
			{
				return Controller->PlayerCameraManager->GetCameraLocation();
			}
		}
	}
	return CameraMapping.Anchor;
}

void UFractalControlSubsystem::PushParameters() const
{
	if (TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> Extension = GetFractalViewExtension())
	{
		Extension->SetFractalParameters(FractalParameters);
	}
}

void UFractalControlSubsystem::PushCameraMapping() const
{
	if (TSharedPtr<FFractalSceneViewExtension, ESPMode::ThreadSafe> Extension = GetFractalViewExtension())
	{
		Extension->SetCameraMapping(CameraMapping);
	}
}
