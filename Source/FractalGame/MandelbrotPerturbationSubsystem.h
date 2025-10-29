#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MandelbrotPerturbationSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;

/**
 * Generates high precision Mandelbrot reference orbits and uploads them for GPU perturbation rendering.
 */
UCLASS()
class FRACTALGAME_API UMandelbrotPerturbationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Assigns the post process material instance that will receive perturbation parameters. */
	UFUNCTION(BlueprintCallable, Category = "Fractals|Perturbation")
	void SetTargetMaterial(UMaterialInstanceDynamic* InMaterial);

	/**
	 * Updates viewport configuration and regenerates the orbit texture when needed.
	 * @param Center 3D world space center for the Mandelbulb reference orbit.
	 * @param Power Mandelbulb power parameter.
	 * @param MaxIterations Requested orbit length.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fractals|Perturbation")
	void SetViewportParameters(const FVector& Center, double Power, int32 MaxIterations);

	/** Forces the subsystem to rebuild the orbit texture using the latest settings. */
	UFUNCTION(BlueprintCallable, Category = "Fractals|Perturbation")
	void ForceRebuild();

	/** Retrieves the orbit texture used for perturbation. */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation")
	UTexture2D* GetOrbitTexture() const { return OrbitTexture; }

	/** Returns the viewport center (X, Y, Z). */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation")
	FVector GetViewportCenter() const { return ViewportCenter; }

	/** Returns the Mandelbulb power parameter. */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation")
	double GetPower() const { return Power; }

private:
	void EnsureOrbitTexture(int32 DesiredLength);
	void BuildOrbit(int32 OrbitLength);
	void UploadOrbitData(const TArray<FVector4f>& OrbitData);
	void PushParametersToMaterial();

private:
	static constexpr int32 MaxSupportedIterations = 32768;
	static constexpr int32 OrbitTextureRows = 4;

	TWeakObjectPtr<UMaterialInstanceDynamic> TargetMaterial;

	UPROPERTY()
	TObjectPtr<UTexture2D> OrbitTexture = nullptr;

	FVector ViewportCenter = FVector::ZeroVector;
	double Power = 8.0;
	int32 CachedOrbitLength = 0;

	bool bOrbitDirty = true;
};
