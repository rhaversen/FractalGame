#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
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

	// Enable/disable fractal rendering
	UFUNCTION(BlueprintCallable, Category = "Fractal")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "Fractal")
	bool IsEnabled() const { return bEnabled; }

	// Set fractal center point
	UFUNCTION(BlueprintCallable, Category = "Fractal")
	void SetCenter(FVector2D InCenter);

	UFUNCTION(BlueprintPure, Category = "Fractal")
	FVector2D GetCenter() const { return Center; }

private:
	UPROPERTY()
	bool bEnabled;

	UPROPERTY()
	FVector2D Center;

	// Update the scene view extension with current parameters
	void UpdateSceneViewExtension();
};
