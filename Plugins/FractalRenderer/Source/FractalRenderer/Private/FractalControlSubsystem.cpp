#include "FractalControlSubsystem.h"
#include "FractalRenderer.h"
#include "FractalSceneViewExtension.h"
#include "HAL/PlatformTime.h"
#include "RHI.h"
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

void UFractalControlSubsystem::SetTargetFrameRate(float InFramesPerSecond)
{
	FractalParameters.TargetFrameRate = FMath::Max(InFramesPerSecond, 0.0f);
	if (FractalParameters.TargetFrameRate <= 0.0f)
	{
		FractalParameters.ResolutionScale = 1.0f;
	}
	PushParameters();
}

void UFractalControlSubsystem::UpdateAdaptiveResolution(float DeltaSeconds)
{
	if (!(DeltaSeconds > 0.0f))
	{
		return;
	}

	// GPU frame time of the last completed frame; the frame delta is the fallback (and is capped by v-sync, which
	// is why the GPU time is preferred: it shows headroom below the refresh rate).
	float FrameMilliseconds = DeltaSeconds * 1000.0f;
	const uint32 GpuCycles = RHIGetGPUFrameCycles();
	if (GpuCycles > 0)
	{
		FrameMilliseconds = static_cast<float>(FPlatformTime::ToMilliseconds(GpuCycles));
	}
	// Smooth over ~0.3 s so single slow frames do not move the resolution.
	const float Blend = 1.0f - FMath::Exp(-DeltaSeconds / 0.3f);
	MeasuredFrameMilliseconds = MeasuredFrameMilliseconds > 0.0f ? FMath::Lerp(MeasuredFrameMilliseconds, FrameMilliseconds, Blend) : FrameMilliseconds;

	float& Scale = FractalParameters.ResolutionScale;
	const float TargetFrameRate = FractalParameters.TargetFrameRate;
	if (TargetFrameRate <= 0.0f)
	{
		if (Scale != 1.0f)
		{
			Scale = 1.0f;
			PushParameters();
		}
		return;
	}

	// Cost follows the pixel count, i.e. the square of the scale (measured in Tools/PerturbationLab: 0.5 -> ~0.2x).
	// Drop quickly when over budget, climb back slowly when there is headroom, and change at most every 0.25 s.
	SecondsSinceResolutionChange += DeltaSeconds;
	if (SecondsSinceResolutionChange < 0.25f)
	{
		return;
	}
	const float TargetMilliseconds = 1000.0f / TargetFrameRate;
	const float Ratio = MeasuredFrameMilliseconds / TargetMilliseconds;
	float NewScale = Scale;
	if (Ratio > 1.05f)
	{
		NewScale = Scale * FMath::Clamp(FMath::Sqrt(1.0f / Ratio), 0.7f, 0.99f);
	}
	else if (Ratio < 0.85f && Scale < 1.0f)
	{
		NewScale = Scale * FMath::Min(FMath::Sqrt(1.0f / Ratio), 1.06f);
	}
	NewScale = FMath::Clamp(NewScale, FMath::Clamp(FractalParameters.MinResolutionScale, 0.1f, 1.0f), 1.0f);
	if (NewScale > 0.97f)
	{
		NewScale = 1.0f; // the last few percent are not worth an upscale pass
	}
	if (!FMath::IsNearlyEqual(NewScale, Scale, 0.005f))
	{
		Scale = NewScale;
		SecondsSinceResolutionChange = 0.0f;
		PushParameters();
	}
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
	// Unzoomed: scale the camera's fractal position about the fractal origin (the camera stays where it is in
	// the world), so the fractal grows or shrinks around its centre. Zoomed: the camera is at a tiny feature far
	// from the origin and moving it by (Factor - 1) * |position| would throw it out of the picture, so it keeps its
	// fractal position and only the units change.
	const double Factor = InUserScale / UserScale;
	const FVector3d Camera = GetCameraWorldLocation();
	const FDDVec3 CameraFractal = CameraMapping.WorldToFractal(Camera);
	const bool bZoomed = GetZoomFactor() < 1.0 - 1.0e-9;
	CameraMapping.Origin = bZoomed ? CameraFractal : FDDVec3(CameraFractal.X * Factor, CameraFractal.Y * Factor, CameraFractal.Z * Factor);
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
	CameraMapping.Scale = NewScale; // exact at the limits (Scale * (Limit / Scale) need not round to Limit)
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
	Params.MaxIterations = FractalMath::ComputeIterationBudget(Formula, Params.Power, FractalParameters.MaxIterations, Params.Bailout, CameraMapping.Scale);

	const FDDVec3 Position = CameraMapping.WorldToFractal(WorldPosition);
	const FractalMath::FDistanceEstimate DE = FractalMath::DistanceEstimateAtScale(Formula, Position, CameraMapping.Scale, Params);

	FFractalDistanceInfo Info;
	Info.FractalDistance = DE.SafeDistance();
	Info.WorldDistance = DE.SafeDistance() / CameraMapping.Scale;
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

	// Integer part, then the fraction digit by digit (each step multiplies by 10 in double-double, so digits beyond
	// the ~32 significant ones of a double-double are noise).
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

	// A value that rounds to zero is printed without a sign.
	bool bNonZero = IntPart != 0.0;
	for (const int32 Digit : Digits)
	{
		bNonZero = bNonZero || Digit != 0;
	}
	FString Result = FString::Printf(TEXT("%s%.0f"), (bNegative && bNonZero) ? TEXT("-") : TEXT(""), IntPart);
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
