#pragma once

#include "CoreMinimal.h"
#include "FractalParameter.generated.h"

/**
 * Rendering tunables. The mapping between world space and fractal space (position and scale) is
 * owned by UFractalControlSubsystem in double-double precision, see FFractalCameraMapping.
 */
USTRUCT(BlueprintType)
struct FRACTALRENDERER_API FFractalParameter
{
    GENERATED_BODY()

public:
    /** Master enable so gameplay can toggle rendering without destroying state. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Viewport")
    bool bEnabled;

    /** Maximum number of distance-estimation steps performed per ray. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Ray March")
    int32 MaxRaySteps;

    /** Maximum world-space distance a ray may travel before we treat it as a miss. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Ray March")
    float MaxRayDistance;

    /** Upper bound for distance-estimator iterations per sample (also the reference orbit length). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
    int32 MaxIterations;

    /** Escape radius that determines when a sample is considered outside the set. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
    float BailoutRadius;

    /** Minimum iterations guaranteed before adaptive stopping kicks in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
    int32 MinIterations;

    /** Threshold factor (of the pixel radius) used to end iterations once the DE stabilises. 0 disables it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
    float ConvergenceFactor;

    /** Power used by the Mandelbulb formula (8 produces the classic bulb). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Formula")
    float FractalPower;

    /**
     * Fractal-space pixel footprint above which a march sample uses plain float iteration instead of
     * perturbation (it is exact enough there and ~1.7x cheaper). 0 = always use perturbation.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Precision")
    float DirectEvaluationFootprint;

    FFractalParameter()
        : bEnabled(true)
        , MaxRaySteps(150)
        , MaxRayDistance(1000000.0f)
        , MaxIterations(150)
        , BailoutRadius(10.0f)
        , MinIterations(5)
        , ConvergenceFactor(0.0f)
        , FractalPower(8.0f)
        , DirectEvaluationFootprint(1.0e-4f)
    {
    }
};
