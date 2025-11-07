#pragma once

#include "CoreMinimal.h"

/**
 * High-precision orbit data for a single iteration
 */
struct FOrbitPoint
{
	FVector3d Position;      // z_n in double precision
	FVector3d Derivative;    // dz_n/dc in double precision
	int32 Iteration;         // Iteration index
	bool bEscaped;          // Whether this point exceeded bailout

	FOrbitPoint()
		: Position(FVector3d::ZeroVector)
		, Derivative(FVector3d::ZeroVector)
		, Iteration(0)
		, bEscaped(false)
	{
	}

	FOrbitPoint(const FVector3d& InPosition, const FVector3d& InDerivative, int32 InIteration, bool InEscaped)
		: Position(InPosition)
		, Derivative(InDerivative)
		, Iteration(InIteration)
		, bEscaped(InEscaped)
	{
	}
};

/**
 * Complete reference orbit data
 */
struct FReferenceOrbit
{
	TArray<FOrbitPoint> Points;    // Sequence z_0, z_1, ..., z_N
	FVector3d ReferenceCenter;     // C_0 in fractal space
	double Power;                   // Fractal power (typically 8.0)
	double BailoutRadius;          // Escape threshold
	int32 EscapeIteration;         // Iteration where orbit escaped (-1 if never)
	bool bValid;                   // Whether orbit is valid for use
	bool bHasDerivatives;          // Whether derivative data has been populated

	FReferenceOrbit()
		: ReferenceCenter(FVector3d::ZeroVector)
		, Power(8.0)
		, BailoutRadius(2.0)
		, EscapeIteration(-1)
		, bValid(false)
		, bHasDerivatives(false)
	{
	}

	/** Get orbit length */
	int32 GetLength() const { return Points.Num(); }

	/** Check if orbit is valid and usable */
	bool IsValid() const { return bValid && Points.Num() > 0; }

	/** Helper to query derivative availability */
	bool HasDerivatives() const { return bHasDerivatives; }
};

/**
 * Generates high-precision reference orbits for Mandelbulb perturbation rendering.
 * 
 * This class computes the reference orbit sequence z_0, z_1, ..., z_N in double precision
 * following the 3D Mandelbulb formula:
 *   z_{n+1} = g_p(z_n) + C_0
 * where g_p is the spherical power transform.
 * 
 * The orbit is computed on the CPU in double precision and can be uploaded to the GPU
 * for perturbation-based rendering, allowing deep zooms beyond single-precision limits.
 */
class FRACTALRENDERER_API FMandelbulbOrbitGenerator
{
public:
	FMandelbulbOrbitGenerator();
	~FMandelbulbOrbitGenerator();

	/**
	 * Generate a reference orbit for the given parameters.
	 * 
	 * @param ReferenceCenter - C_0, the reference point in fractal space (typically viewport center)
	 * @param Power - Fractal power p (typically 8.0 for classic Mandelbulb)
	 * @param MaxIterations - Maximum number of iterations to compute
	 * @param BailoutRadius - Escape threshold (typically 2.0)
	 * @return Reference orbit data
	 */
	FReferenceOrbit GenerateOrbit(
		const FVector3d& ReferenceCenter,
		double Power,
		int32 MaxIterations,
		double BailoutRadius
	) const;

	/**
	 * Convert high-precision orbit to float format for GPU upload.
	 * Packs orbit points and derivatives as float4 arrays (x, y, z, unused).
	 * 
	 * @param Orbit - Source orbit in double precision
	 * @param OutPositionData - Destination array for position float4 values
	 * @param OutDerivativeData - Destination array for derivative float4 values
	 */
	static void ConvertOrbitToFloat(
		const FReferenceOrbit& Orbit,
		TArray<FVector4f>& OutPositionData,
		TArray<FVector4f>& OutDerivativeData
	);

	/**
	 * Compute a single Mandelbulb iteration: z_new = g_p(z) + C
	 * Uses spherical coordinate transformation as per research.
	 * 
	 * @param Z - Current orbit point
	 * @param C - Constant to add (reference center)
	 * @param Power - Fractal power
	 * @return Next orbit point z_{n+1}
	 */
	static FVector3d MandelbulbIteration(
		const FVector3d& Z,
		const FVector3d& C,
		double Power
	);

private:
	/**
	 * Convert Cartesian coordinates to spherical.
	 * Returns (r, theta, phi) where:
	 *   r = sqrt(x^2 + y^2 + z^2)
	 *   theta = acos(z/r)  [polar angle, 0 to pi]
	 *   phi = atan2(y, x)  [azimuthal angle, -pi to pi]
	 */
	static FVector3d CartesianToSpherical(const FVector3d& Cartesian);

	/**
	 * Convert spherical coordinates to Cartesian.
	 * Input: (r, theta, phi)
	 * Returns: (x, y, z) = (r*sin(theta)*cos(phi), r*sin(theta)*sin(phi), r*cos(theta))
	 */
	static FVector3d SphericalToCartesian(double R, double Theta, double Phi);

	/**
	 * Apply spherical power transform: g_p(z)
	 * Transforms (r, theta, phi) to (r^p, p*theta, p*phi) then converts back to Cartesian.
	 */
	static FVector3d SphericalPowerTransform(const FVector3d& Z, double Power);
};
