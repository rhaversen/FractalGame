#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FractalParameter.h"
#include "FractalControlSubsystem.generated.h"

/**
 * Game Instance Subsystem for controlling fractal rendering parameters
 * Access from Blueprint or C++ to control the Scene View Extension
 */
UCLASS()
class FRACTALRENDERER_API UFractalControlSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Set all fractal parameters
	UFUNCTION(BlueprintCallable, Category = "Fractal")
	void SetFractalParameters(const FFractalParameter& InParams);

	// Individual setters so gameplay code can tweak a single property without copying the struct
	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetCenter(FVector2D InCenter);

	UFUNCTION(BlueprintCallable, Category = "Fractal|Controls")
	void SetZoom(float InZoom);

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

	// Get current fractal parameters
	UFUNCTION(BlueprintPure, Category = "Fractal")
	const FFractalParameter& GetFractalParameters() const { return FractalParameters; }

private:
	UPROPERTY()
	FFractalParameter FractalParameters;

	// Update the scene view extension with current parameters
	void UpdateSceneViewExtension();
};
