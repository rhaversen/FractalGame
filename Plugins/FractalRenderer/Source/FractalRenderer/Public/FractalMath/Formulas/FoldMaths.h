// Box and sphere folds shared by the folding formulas (Mandelbox, Kaleidoscopic IFS), in any precision,
// plus the precise quantities their exact GPU perturbation needs (Shaders/FractalFolds.ush) and their
// series-skip Jacobians.
//
//   box fold (limit 1):        x -> 2 - x (x > 1),  -2 - x (x < -1),  x otherwise      (per component)
//   sphere fold (r_min 0.5, r_fixed 1): z -> k z,  dr -> k dr  with
//       k = r_fixed^2 / r_min^2 = 4 (r^2 < r_min^2),  r_fixed^2 / r^2 (r^2 < r_fixed^2),  1 otherwise
// Both are continuous; their branch boundaries are where the GPU needs exact reference differences.

#pragma once

#include "../FractalScalar.h"

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

namespace Folds
{
	constexpr double BoxLimit = 1.0;
	constexpr double MinRadius2 = 0.25;   // r_min = 0.5
	constexpr double FixedRadius2 = 1.0;  // r_fixed = 1

	template <typename T>
	inline T BoxFoldComponent(const T& X)
	{
		if (X > T(BoxLimit))
		{
			return T(2.0 * BoxLimit) - X;
		}
		if (X < T(-BoxLimit))
		{
			return T(-2.0 * BoxLimit) - X;
		}
		return X;
	}

	template <typename T>
	inline TFractalVec3<T> BoxFold(const TFractalVec3<T>& Z)
	{
		return TFractalVec3<T>(BoxFoldComponent(Z.X), BoxFoldComponent(Z.Y), BoxFoldComponent(Z.Z));
	}

	/** Sphere fold applied to Z; returns the factor k (also applied to dr). */
	template <typename T>
	inline TFractalVec3<T> SphereFold(const TFractalVec3<T>& Z, double& OutK)
	{
		const T R2 = Dot(Z, Z);
		if (R2 < T(MinRadius2))
		{
			OutK = FixedRadius2 / MinRadius2;
			return Z * OutK;
		}
		if (R2 < T(FixedRadius2))
		{
			OutK = FixedRadius2 / RToDouble(R2);
			const T Factor = T(FixedRadius2) / R2;
			return TFractalVec3<T>(Z.X * Factor, Z.Y * Factor, Z.Z * Factor);
		}
		OutK = 1.0;
		return Z;
	}

	/**
	 * Precise sphere-fold block (1 float4) for the reference point B (the vector entering the sphere fold):
	 *   (min_r^2 - |B|^2, fixed_r^2 - |B|^2, |B|^2, 0)
	 * The differences are rounded from T precision, so their signs reproduce the CPU's branch exactly and
	 * they keep relative accuracy next to the boundaries.
	 */
	template <typename T>
	inline void PackSphereFold(const TFractalVec3<T>& B, float* Out)
	{
		const T R2 = Dot(B, B);
		Out[0] = ClampToFloatRange(RToDouble(T(MinRadius2) - R2));
		Out[1] = ClampToFloatRange(RToDouble(T(FixedRadius2) - R2));
		Out[2] = ClampToFloatRange(RToDouble(R2));
		Out[3] = 0.0f;
	}

	/**
	 * Series step through the sphere fold at B: multiplies J (3x3, rows of the map so far) by the fold's
	 * Jacobian and returns the largest |d| for which the pixel stays in the reference's branch and the
	 * inversion stays linear to relative accuracy Tolerance.
	 */
	inline double SphereFoldSeries(const TFractalVec3<double>& B, double Tolerance, double J[3][3])
	{
		const double R2 = Dot(B, B);
		const double R = std::sqrt(R2);
		const double ToMin = std::fabs(R - std::sqrt(MinRadius2));
		const double ToFixed = std::fabs(R - std::sqrt(FixedRadius2));
		double Limit = ToMin < ToFixed ? ToMin : ToFixed;
		double F[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
		if (R2 < MinRadius2 || !(R2 < FixedRadius2))
		{
			const double K = R2 < MinRadius2 ? FixedRadius2 / MinRadius2 : 1.0;
			for (int I = 0; I < 3; ++I)
			{
				F[I][I] = K;
			}
		}
		else
		{
			// d/dB (r_f^2 B / |B|^2) = r_f^2 (I - 2 B B^T / |B|^2) / |B|^2; the relative linearisation error of
			// the inversion is ~ |d| / |B|.
			for (int I = 0; I < 3; ++I)
			{
				for (int K = 0; K < 3; ++K)
				{
					F[I][K] = FixedRadius2 * ((I == K ? 1.0 : 0.0) - 2.0 * B[I] * B[K] / R2) / R2;
				}
			}
			const double Smooth = Tolerance * R;
			Limit = Smooth < Limit ? Smooth : Limit;
		}
		double Out[3][3];
		for (int I = 0; I < 3; ++I)
		{
			for (int K = 0; K < 3; ++K)
			{
				Out[I][K] = F[I][0] * J[0][K] + F[I][1] * J[1][K] + F[I][2] * J[2][K];
			}
		}
		for (int I = 0; I < 3; ++I)
		{
			for (int K = 0; K < 3; ++K)
			{
				J[I][K] = Out[I][K];
			}
		}
		return Limit;
	}
} // namespace Folds

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
