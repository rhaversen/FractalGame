#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FractalParameter.h"
#include "FractalReference.h"
#include "FractalControlSubsystem.generated.h"

/**
 * Game Instance Subsystem for controlling fractal rendering.
 *
 * World space and fractal space are related by Fractal = Origin + (World - Anchor) * Scale, with Origin in
 * double-double precision. Gameplay keeps moving the pawn in ordinary world units; zooming changes Scale
 * around the camera (so nothing jumps) and therefore also scales how far the pawn travels in the fractal.
 * The renderer perturbs every pixel from a high-precision reference ray, so the camera can go down to a
 * scale of ~1e-27 fractal units per world unit.
 */
UCLASS()
class FRACTALRENDERER_API UFractalControlSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Rendering tunables ---

	UFUNCTION(BlueprintCallable, Category = "Fractal")
	void SetFractalParameters(const FFractalParameter& InParams);

	UFUNCTION(BlueprintPure, Category = "Fractal")
	const FFractalParameter& GetFractalParameters() const { return FractalParameters; }

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetMaxRaySteps(int32 InMaxRaySteps);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetMaxRayDistance(float InMaxRayDistance);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetMaxIterations(int32 InMaxIterations);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetBailoutRadius(float InBailoutRadius);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetMinIterations(int32 InMinIterations);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetConvergenceFactor(float InConvergenceFactor);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetFractalPower(float InFractalPower);

	// --- World <-> fractal mapping ---

	/** Fractal units per world unit. Smaller = deeper zoom. Changed around the camera position. */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Camera")
	void SetFractalScale(double InScale);

	UFUNCTION(BlueprintPure, Category = "Fractal|Camera")
	double GetFractalScale() const { return CameraMapping.Scale; }

	/** Multiplies the scale by Factor around the camera (Factor < 1 zooms in). */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Camera")
	void ZoomAroundCamera(double Factor);

	/** Moves the fractal so that the camera sits at FractalPosition (double precision). */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Camera")
	void SetCameraFractalPosition(FVector FractalPosition);

	/** Camera position in fractal space, rounded to double. */
	UFUNCTION(BlueprintPure, Category = "Fractal|Camera")
	FVector GetCameraFractalPosition() const;

	/** Distance from the camera to the fractal surface in world units (from the latest reference march). */
	UFUNCTION(BlueprintPure, Category = "Fractal|Camera")
	double GetCameraDistanceEstimate() const;

	/** Restores the default mapping (world origin at fractal origin, scale 1e-5). */
	UFUNCTION(BlueprintCallable, Category = "Fractal|Camera")
	void ResetCameraMapping();

	/** Full-precision access for C++. */
	const FFractalCameraMapping& GetCameraMapping() const { return CameraMapping; }
	void SetCameraMapping(const FFractalCameraMapping& InMapping);

private:
	UPROPERTY()
	FFractalParameter FractalParameters;

	FFractalCameraMapping CameraMapping;

	/** Current camera location in world space (falls back to the mapping anchor). */
	FVector3d GetCameraWorldLocation() const;

	void PushParameters() const;
	void PushCameraMapping() const;
};
