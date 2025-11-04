#pragma once

#include "CoreMinimal.h"
#include "FractalParameter.generated.h"

USTRUCT(BlueprintType)
struct FRACTALRENDERER_API FFractalParameter
{
    GENERATED_BODY()

public:
    /** Location in the complex plane that the camera is focused on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Viewport")
    FVector2D Center;

    /** Master enable so gameplay can toggle rendering without destroying state. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Viewport")
    bool bEnabled;

    /** Scale multiplier applied to world rays before marching (behaves like zoom). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Viewport")
    float Zoom;

    /** Maximum number of distance-estimation steps performed per ray. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Ray March")
    int32 MaxRaySteps;

    /** Maximum world-space distance a ray may travel before we treat it as a miss. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Ray March")
    float MaxRayDistance;

    /** Upper bound for distance-estimator iterations per sample. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
    int32 MaxIterations;

    /** Escape radius that determines when a sample is considered outside the set. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
    float BailoutRadius;

    /** Minimum iterations guaranteed before adaptive stopping kicks in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
    int32 MinIterations;

    /** Threshold factor used to end iterations once the DE stabilises. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
    float ConvergenceFactor;

    /** Power used by the Mandelbulb formula (8 produces the classic bulb). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Formula")
    float FractalPower;

    FFractalParameter()
        : Center(FVector2D::ZeroVector)
        , bEnabled(true)
        , Zoom(0.00001f)
        , MaxRaySteps(150)
        , MaxRayDistance(1000000.0f)
        , MaxIterations(150)
        , BailoutRadius(10.0f)
        , MinIterations(5)
        , ConvergenceFactor(0.01f)
        , FractalPower(8.0f)
    {
    }
};
