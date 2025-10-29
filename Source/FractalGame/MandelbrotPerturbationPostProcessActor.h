#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MandelbrotPerturbationPostProcessActor.generated.h"

class UPostProcessComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Helper actor that owns a post-process component hooked up to the Mandelbrot perturbation material.
 * It creates a MID at runtime, adds it to the blendables list, and informs the perturbation subsystem.
 */
UCLASS()
class FRACTALGAME_API AMandelbrotPerturbationPostProcessActor : public AActor
{
    GENERATED_BODY()

public:
    AMandelbrotPerturbationPostProcessActor();

    /** Returns the runtime dynamic material instance, or nullptr if it was not created yet. */
    UFUNCTION(BlueprintCallable, Category = "Fractals|Perturbation")
    UMaterialInstanceDynamic* GetDynamicMaterialInstance() const { return DynamicMaterialInstance; }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    /** Sets up the post process component with the target material and registers it with the subsystem. */
    void InitialiseMaterialBinding();
    void UpdateViewportFromCamera();

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UPostProcessComponent> PostProcessComponent;

    /** Base perturbation material (M_FractalPerturbation). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fractals|Perturbation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UMaterialInterface> PerturbationMaterial;

    /** Blend weight applied when adding the material to the post-process stack. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fractals|Perturbation", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
    float BlendWeight;

    /** Pans the fractal by mapping camera translation (in UU) onto the complex plane axes. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fractals|Perturbation", meta = (AllowPrivateAccess = "true"))
    float CameraPanScale;

    /** Scales the viewport in response to camera motion along the actor's forward axis. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fractals|Perturbation", meta = (AllowPrivateAccess = "true"))
    float CameraZoomScale;

    /** Iteration depth pushed into the perturbation subsystem each frame. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fractals|Perturbation", meta = (AllowPrivateAccess = "true", ClampMin = "1", UIMin = "1"))
    int32 IterationCount;

    /** Cached dynamic instance that is shared with the perturbation subsystem. */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DynamicMaterialInstance;

    bool bHasCachedCameraState;
    FVector InitialViewLocation;
    FVector InitialViewportCenter;
    FVector LastPushedCenter;
    double LastPushedPower;
};
