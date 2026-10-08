#pragma once

#include "CoreMinimal.h"
#include "FractalMath/FractalFormulaTypes.h"
#include "FractalParameter.generated.h"

/** The fractal formulas. Values match FractalMath::EFractalFormula and FP_FRACTAL_TYPE in the shaders. */
UENUM(BlueprintType)
enum class EFractalType : uint8
{
	Mandelbulb = 0,
	BurningShip = 1 UMETA(DisplayName = "Burning Ship"),
	JuliaSet = 2 UMETA(DisplayName = "Julia Set"),
	Mandelbox = 3,
	InvertedMenger = 4 UMETA(DisplayName = "Inverted Menger"),
	Quaternion = 5,
	SierpinskiTetrahedron = 6 UMETA(DisplayName = "Sierpinski Tetrahedron"),
	KaleidoscopicIFS = 7 UMETA(DisplayName = "Kaleidoscopic IFS"),
	Count UMETA(Hidden)
};

static_assert(static_cast<int32>(EFractalType::Count) == FractalMath::FractalFormulaCount, "EFractalType must list every FractalMath::EFractalFormula");

inline FractalMath::EFractalFormula ToFractalFormula(EFractalType Type)
{
	return static_cast<FractalMath::EFractalFormula>(Type);
}

/**
 * Rendering tunables. The mapping between world space and fractal space (position and scale) is
 * owned by UFractalControlSubsystem in double-double precision, see FFractalCameraMapping.
 */
USTRUCT(BlueprintType)
struct FRACTALRENDERER_API FFractalParameter
{
	GENERATED_BODY()

	/** Master enable so gameplay can toggle rendering without destroying state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Viewport")
	bool bEnabled = true;

	/** Which fractal is rendered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Formula")
	EFractalType FractalType = EFractalType::Mandelbulb;

	/** Exponent of the power fractals (8 = classic Mandelbulb), scale factor of the folding fractals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Formula")
	float FractalPower = 8.0f;

	/** Maximum number of distance-estimation steps performed per ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Ray March")
	int32 MaxRaySteps = 150;

	/** Distance (in fractal units) a ray may travel before it counts as a miss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Ray March")
	float MaxRayDistance = 500.0f;

	/**
	 * Distance-estimator iterations per sample (also the reference orbit length). Folding fractals get more
	 * automatically when the zoom is so deep that their orbits need longer to escape.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
	int32 MaxIterations = 150;

	/** Escape radius that determines when a sample is considered outside the set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Distance Estimation")
	float BailoutRadius = 10.0f;

	/**
	 * Fractal-space pixel footprint above which a march sample uses plain float iteration instead of
	 * perturbation (it is exact enough there and cheaper). 0 = always use perturbation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fractal|Precision")
	float DirectEvaluationFootprint = 1.0e-4f;
};
