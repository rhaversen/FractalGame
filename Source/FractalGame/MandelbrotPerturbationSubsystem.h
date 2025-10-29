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
	 * @param Center Complex plane center in world units.
	 * @param Scale Horizontal span of the viewport in complex plane coordinates.
	 * @param Aspect Aspect ratio (width / height). When non-positive the subsystem will derive it from the view.
	 * @param MaxIterations Requested orbit length.
	 */
	UFUNCTION(BlueprintCallable, Category = "Fractals|Perturbation")
	void SetViewportParameters(const FVector2D& Center, double Scale, double Aspect, int32 MaxIterations);

	/** Forces the subsystem to rebuild the orbit texture using the latest settings. */
	UFUNCTION(BlueprintCallable, Category = "Fractals|Perturbation")
	void ForceRebuild();

	/** Retrieves the orbit texture used for perturbation. */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation")
	UTexture2D* GetOrbitTexture() const { return OrbitTexture; }

	/** Returns the viewport center (X, Y). */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation")
	FVector2D GetViewportCenter() const { return ViewportCenter; }

	/** Returns the viewport scale and aspect (Scale, Aspect). */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation")
	FVector2D GetViewportScaleAspect() const { return FVector2D(ViewportScale, ViewportAspect); }

	/** Returns the reference center high-precision component (Hi.x, Hi.y). */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation")
	FVector2D GetReferenceHi() const { return ReferenceCenterHi; }

	/** Returns the reference center low-precision component (Lo.x, Lo.y). */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation")
	FVector2D GetReferenceLo() const { return ReferenceCenterLo; }

	/** Legacy: Returns the viewport vector used by the material (Center.x, Center.y, Scale, Aspect). */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation", meta = (DeprecatedFunction, DeprecationMessage = "Use GetViewportCenter and GetViewportScaleAspect instead"))
	FVector4 GetViewportVector() const;

	/** Legacy: Returns the reference center split into hi/lo components (Hi.x, Hi.y, Lo.x, Lo.y). */
	UFUNCTION(BlueprintPure, Category = "Fractals|Perturbation", meta = (DeprecatedFunction, DeprecationMessage = "Use GetReferenceHi and GetReferenceLo instead"))
	FVector4 GetReferenceVector() const;

private:
	void EnsureOrbitTexture(int32 DesiredLength);
	void BuildOrbit(int32 OrbitLength);
	void UploadOrbitData(const TArray<FVector4f>& OrbitData);
	void PushParametersToMaterial();
	void UpdateReferenceSplit();

private:
	static constexpr int32 MaxSupportedIterations = 32768;

	TWeakObjectPtr<UMaterialInstanceDynamic> TargetMaterial;

	UPROPERTY()
	TObjectPtr<UTexture2D> OrbitTexture = nullptr;

	FVector2D ViewportCenter = FVector2D::ZeroVector;
	double ViewportScale = 3.0;
	double ViewportAspect = 1.0;
	int32 CachedOrbitLength = 0;

	long double ReferenceCenterX = 0.0L;
	long double ReferenceCenterY = 0.0L;

	FVector2D ReferenceCenterHi = FVector2D::ZeroVector;
	FVector2D ReferenceCenterLo = FVector2D::ZeroVector;

	bool bOrbitDirty = true;
};
