#pragma once

#include "CoreMinimal.h"

/**
 * 3x3 Jacobian storage for Mandelbulb orbit derivatives.
 * Columns represent the partial derivatives of z with respect to C's x/y/z components.
 */
struct FOrbitDerivative
{
	FVector3d Columns[3];

	FOrbitDerivative()
	{
		Columns[0] = FVector3d::ZeroVector;
		Columns[1] = FVector3d::ZeroVector;
		Columns[2] = FVector3d::ZeroVector;
	}

	static FOrbitDerivative Zero()
	{
		return FOrbitDerivative();
	}

	static FOrbitDerivative Identity()
	{
		FOrbitDerivative Result;
		Result.Columns[0] = FVector3d(1.0, 0.0, 0.0);
		Result.Columns[1] = FVector3d(0.0, 1.0, 0.0);
		Result.Columns[2] = FVector3d(0.0, 0.0, 1.0);
		return Result;
	}

	FOrbitDerivative operator+(const FOrbitDerivative& Other) const
	{
		FOrbitDerivative Result;
		Result.Columns[0] = Columns[0] + Other.Columns[0];
		Result.Columns[1] = Columns[1] + Other.Columns[1];
		Result.Columns[2] = Columns[2] + Other.Columns[2];
		return Result;
	}

	FOrbitDerivative operator-(const FOrbitDerivative& Other) const
	{
		FOrbitDerivative Result;
		Result.Columns[0] = Columns[0] - Other.Columns[0];
		Result.Columns[1] = Columns[1] - Other.Columns[1];
		Result.Columns[2] = Columns[2] - Other.Columns[2];
		return Result;
	}

	FOrbitDerivative operator*(double Scalar) const
	{
		FOrbitDerivative Result;
		Result.Columns[0] = Columns[0] * Scalar;
		Result.Columns[1] = Columns[1] * Scalar;
		Result.Columns[2] = Columns[2] * Scalar;
		return Result;
	}

	FVector3d TransformVector(const FVector3d& Vector) const
	{
		return Columns[0] * Vector.X + Columns[1] * Vector.Y + Columns[2] * Vector.Z;
	}

	FOrbitDerivative Multiply(const FOrbitDerivative& Other) const
	{
		FOrbitDerivative Result;
		Result.Columns[0] = TransformVector(Other.Columns[0]);
		Result.Columns[1] = TransformVector(Other.Columns[1]);
		Result.Columns[2] = TransformVector(Other.Columns[2]);
		return Result;
	}

	void SetColumn(int32 Index, const FVector3d& Value)
	{
		check(Index >= 0 && Index < 3);
		Columns[Index] = Value;
	}

	const FVector3d& GetColumn(int32 Index) const
	{
		check(Index >= 0 && Index < 3);
		return Columns[Index];
	}
};

/** Packed GPU-friendly orbit sample (float4-aligned). */
struct FPackedOrbitSample
{
	FVector4f Position;
	FVector4f TransformJacobian[3];

	FPackedOrbitSample()
		: Position(FVector4f::Zero())
	{
		TransformJacobian[0] = FVector4f::Zero();
		TransformJacobian[1] = FVector4f::Zero();
		TransformJacobian[2] = FVector4f::Zero();
	}
};

/**
 * High-precision orbit data for a single iteration
 */
struct FOrbitPoint
{
	FVector3d Position;          // z_n in double precision
	FOrbitDerivative Derivative;          // 3x3 Jacobian dz_n/dc
	FOrbitDerivative TransformJacobian;  // 3x3 Jacobian of Mandelbulb mapping at z_n
	int32 Iteration;             // Iteration index
	bool bEscaped;               // Whether this point exceeded bailout

	FOrbitPoint()
		: Position(FVector3d::ZeroVector)
		, Derivative(FOrbitDerivative::Zero())
		, TransformJacobian(FOrbitDerivative::Zero())
		, Iteration(0)
		, bEscaped(false)
	{
	}

	FOrbitPoint(
		const FVector3d& InPosition,
		const FOrbitDerivative& InDerivative,
		const FOrbitDerivative& InTransformJacobian,
		int32 InIteration,
		bool bInEscaped)
		: Position(InPosition)
		, Derivative(InDerivative)
		, TransformJacobian(InTransformJacobian)
		, Iteration(InIteration)
		, bEscaped(bInEscaped)
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
	 * Build structured buffer data for GPU upload (single element per iteration).
	 * 
	 * @param Orbit - Source orbit in double precision
	 * @param OutSamples - Destination array of packed samples
	 */
	static void BuildOrbitBuffer(
		const FReferenceOrbit& Orbit,
		TArray<FPackedOrbitSample>& OutSamples
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

	/**
	 * Numerically approximate the Jacobian of the Mandelbulb power transform at Z.
	 */
	static FOrbitDerivative ComputeJacobianFiniteDifference(const FVector3d& Z, double Power);
};
