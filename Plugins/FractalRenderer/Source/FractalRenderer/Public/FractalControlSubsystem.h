#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FractalParameter.h"
#include "FractalReference.h"
#include "FractalControlSubsystem.generated.h"

/** Distance from a point to the fractal surface (CPU estimate, same formula and precision as the renderer). */
USTRUCT(BlueprintType)
struct FRACTALRENDERER_API FFractalDistanceInfo
{
	GENERATED_BODY()

	/** Distance estimate in world units (0 inside the set). */
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	double WorldDistance = 0.0;

	/** The same distance in fractal units. */
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	double FractalDistance = 0.0;

	/** False when the orbit never escaped (the point is inside a power fractal). */
	UPROPERTY(BlueprintReadOnly, Category = "Fractal")
	bool bEscaped = false;
};

/**
 * Game Instance Subsystem for controlling fractal rendering.
 *
 * World space and fractal space are related by Fractal = Origin + (World - Anchor) * Scale, with Origin in
 * double-double precision (FFractalCameraMapping). Two kinds of change are offered:
 *  - SetUserScale: the original renderer's "scale multiplier". The fractal grows or shrinks around its own
 *    origin, i.e. the camera's fractal position is scaled about the fractal origin.
 *  - ZoomAroundCamera: rescales world units around the camera without changing what is seen. Gameplay uses
 *    it to keep world-space distances comfortable while the player flies ever closer to the surface, which
 *    is how the renderer reaches scales of ~1e-30 fractal units per world unit.
 * The renderer perturbs every pixel from a high-precision reference ray, so precision only depends on the
 * visible scale, not on how deep the camera is.
 */
UCLASS()
class FRACTALRENDERER_API UFractalControlSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Smallest supported fractal units per world unit (float pixel offsets on the GPU underflow below ~1e-38). */
	static constexpr double MinFractalScale = 1.0e-30;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// --- Rendering ---

	UFUNCTION(BlueprintCallable, Category = "Fractal")
	void SetFractalParameters(const FFractalParameter& InParams);

	UFUNCTION(BlueprintPure, Category = "Fractal")
	const FFractalParameter& GetFractalParameters() const { return FractalParameters; }

	UFUNCTION(BlueprintCallable, Category = "Fractal")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Formula")
	void SetFractalType(EFractalType InType);

	UFUNCTION(BlueprintPure, Category = "Fractal|Formula")
	EFractalType GetFractalType() const { return FractalParameters.FractalType; }

	/** Exponent of the power fractals, scale factor of the folding fractals. */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Formula")
	void SetFractalPower(float InPower);

	UFUNCTION(BlueprintPure, Category = "Fractal|Formula")
	float GetFractalPower() const { return FractalParameters.FractalPower; }

	/** Interactive power / scale ranges and defaults of a fractal (same values as the original renderer). */
	static const FractalMath::FFractalPreset& GetPreset(EFractalType Type);

	UFUNCTION(BlueprintPure, Category = "Fractal|Formula")
	static FString GetFractalDisplayName(EFractalType Type);

	// --- World <-> fractal mapping ---

	/**
	 * Fractal units per world unit before any camera zoom (the original "scale multiplier"). Changing it
	 * scales the camera's fractal position about the fractal origin, so the fractal appears to grow or shrink
	 * around its centre; the zoom factor is kept.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Camera")
	void SetUserScale(double InUserScale);

	UFUNCTION(BlueprintPure, Category = "Fractal|Camera")
	double GetUserScale() const { return UserScale; }

	/** Current fractal units per world unit (= UserScale * ZoomFactor). */
	UFUNCTION(BlueprintPure, Category = "Fractal|Camera")
	double GetFractalScale() const { return CameraMapping.Scale; }

	/** Scale / UserScale: 1 when not zoomed, 1e-20 at a 1e20x magnification. */
	UFUNCTION(BlueprintPure, Category = "Fractal|Camera")
	double GetZoomFactor() const { return CameraMapping.Scale / UserScale; }

	/**
	 * Multiplies the scale by Factor around the camera (Factor < 1 zooms in). The view does not change: the
	 * camera keeps its fractal position, only world distances (and with them movement speeds) are rescaled.
	 * The scale is kept within [MinFractalScale, UserScale]. Returns the factor actually applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Camera")
	double ZoomAroundCamera(double Factor);

	/** Unzoomed mapping with the fractal origin at FractalOriginWorld (Scale = UserScale). */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Camera")
	void ResetView(FVector FractalOriginWorld);

	// --- Queries ---

	/** High-precision distance estimate from a world position to the fractal surface. */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Camera")
	FFractalDistanceInfo GetDistanceAtWorld(FVector WorldPosition) const;

	/** Camera position in fractal space, rounded to double. */
	UFUNCTION(BlueprintPure, Category = "Fractal|Camera")
	FVector GetCameraFractalPosition() const;

	/** Camera position in fractal space at full (double-double) precision. */
	FractalMath::FDDVec3 GetCameraFractalPositionDD() const;

	/** Fixed-point text of a double-double value with the given number of decimals (0..30). */
	static FString FormatCoordinate(const FractalMath::FDD& Value, int32 Decimals);

	const FFractalCameraMapping& GetCameraMapping() const { return CameraMapping; }

private:
	UPROPERTY()
	FFractalParameter FractalParameters;

	FFractalCameraMapping CameraMapping;
	double UserScale = 1.0e-3;

	/** Current camera location in world space (falls back to the mapping anchor). */
	FVector3d GetCameraWorldLocation() const;

	void PushParameters() const;
	void PushCameraMapping() const;
};
