// CPU side of the perturbation renderer, generic over the fractal formula (FractalFormulaTypes.h).
//
// The GPU never sees absolute fractal coordinates. Every sample point is written as
//     c = C_ref + dc,        z_n = Z_n + d_n
// where C_ref / Z_n (the reference point and its orbit) are computed here in double-double precision
// and dc / d_n (per-pixel offsets) are tracked in float on the GPU (Shaders/FractalDE.ush).
//
// This header contains, templated on the formula F and the scalar type T (double, FDD, quad in the lab):
//   - reference orbit generation and packing into the GPU layout, including the linear series skip,
//   - the high-precision distance estimator and the reference-ray march that picks C_ref.
//
// GPU orbit layout, per point m (F::PayloadFloat4 + F::StateDim float4):
//   [0 .. PayloadFloat4-1]  formula data (F::Pack), starting with the float-rounded Z~_m
//   [PayloadFloat4 + i]     series row i: (A_m[i][0], A_m[i][1], A_m[i][2], extra_i), extra_0 = skip radius,
//                           extra_1 = reference running derivative dr_m, extra_2 = Bailout^2 - |Z_m|^2
// Point 0 is the zero state (the rebase target of formulas that add c); the orbit proper starts at m = 1.

#pragma once

#include <limits>

#include "FractalScalar.h"
#include "FractalFormulaTypes.h"

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

/** Default tolerance of the linear series skip (validated in Tools/PerturbationLab). */
constexpr double DefaultSeriesTolerance = 1.0e-6;

template <typename F>
constexpr int FormulaOrbitStride() { return F::PayloadFloat4 + F::StateDim; }

/**
 * Linear series skip. While the perturbation stays linear, d_{n+1} = J_n d_n (+ dc for formulas that add c)
 * with J_n the Jacobian of the map at Z_n, so d_n = A_n dc with A_1 = E (the embedding of the position into
 * the state), A_{n+1} = J_n A_n (+ E). A sample whose |dc| is below the validity radius of A_m can start
 * iterating at m instead of 1 -- these are exactly the iterations that grow with zoom depth.
 * Validity: |d_n| <= Limit_n for n = 1 .. m-1 (F::SeriesStep), with |d_n| <= ||A_n||_F |dc|. The skip also stops
 * once the reference leaves the pixel bailout (pixels near it escape there too).
 */
template <typename F>
struct TSeriesState
{
	double A[4][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
	double E[4][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
	double Derivative = 1.0;  // dr_m of the reference (dr_1 = 1)
	double Radius = 1.0e30;   // skipping to m = 1 is always valid
	bool bStopped = false;

	TSeriesState()
	{
		// Start() is linear in the position: its columns are the images of the unit vectors.
		for (int Col = 0; Col < 3; ++Col)
		{
			TFractalVec3<double> Unit;
			Unit[Col] = 1.0;
			const typename F::template TState<double> S = F::Start(Unit);
			for (int Row = 0; Row < F::StateDim; ++Row)
			{
				E[Row][Col] = S[Row];
				A[Row][Col] = S[Row];
			}
		}
	}

	void Store(float* Rows) const
	{
		for (int Row = 0; Row < F::StateDim; ++Row)
		{
			for (int Col = 0; Col < 3; ++Col)
			{
				Rows[4 * Row + Col] = ClampToFloatRange(A[Row][Col]);
			}
			Rows[4 * Row + 3] = 0.0f;
		}
		Rows[3] = bStopped ? 0.0f : ClampToFloatRange(Radius);
		Rows[7] = ClampToFloatRange(Derivative);
	}

	void Advance(const typename F::template TState<double>& Z, const FFormulaParams& Params, double Tolerance)
	{
		if (bStopped)
		{
			return;
		}
		if (!(Tolerance > 0.0) || !(Dot(Z, Z) <= Params.Bailout * Params.Bailout))
		{
			bStopped = true;
			return;
		}
		double J[4][4] = {};
		double Limit = F::SeriesStep(Z, Params, Tolerance, J);
		// A pixel that crosses the bailout between two orbit points must not be skipped over: its offset has to
		// stay below half the reference's distance to the bailout sphere.
		const double ToBailout = 0.5 * (Params.Bailout - std::sqrt(Dot(Z, Z)));
		Limit = Limit < ToBailout ? Limit : ToBailout;
		double NormA2 = 0.0;
		for (int Row = 0; Row < F::StateDim; ++Row)
		{
			for (int Col = 0; Col < 3; ++Col)
			{
				NormA2 += A[Row][Col] * A[Row][Col];
			}
		}
		if (!(Limit > 0.0) || !(NormA2 > 0.0))
		{
			bStopped = true;
			return;
		}
		const double RadiusHere = Limit / std::sqrt(NormA2);
		Radius = RadiusHere < Radius ? RadiusHere : Radius;

		double NewA[4][3];
		for (int Row = 0; Row < F::StateDim; ++Row)
		{
			for (int Col = 0; Col < 3; ++Col)
			{
				double Sum = F::bAddsC ? E[Row][Col] : 0.0;
				for (int K = 0; K < F::StateDim; ++K)
				{
					Sum += J[Row][K] * A[K][Col];
				}
				NewA[Row][Col] = Sum;
			}
		}
		for (int Row = 0; Row < F::StateDim; ++Row)
		{
			for (int Col = 0; Col < 3; ++Col)
			{
				A[Row][Col] = NewA[Row][Col];
			}
		}
		Derivative = F::DerivativeStep(Z, Derivative, Params);
	}
};

/**
 * Computes Z_1 = Start(C), Z_{m+1} = Step(Z_m, C) until |Z_m| > F::ReferenceEscapeRadius (that point is kept)
 * or m == MaxIterations, preceded by the zero point 0. Writes FormulaOrbitStride<F>() float4 per point to
 * OutFloats (capacity (MaxIterations + 1) * stride * 4 floats) and optionally the full-precision orbit
 * (index-aligned, OutOrbit[0] = 0). Returns the number of points written (>= 2).
 */
template <typename F, typename T>
inline int GenerateReferenceOrbit(const TFractalVec3<T>& C, const FFormulaParams& Params, float* OutFloats,
	typename F::template TState<T>* OutOrbit = nullptr, double SeriesTolerance = DefaultSeriesTolerance)
{
	using FState = typename F::template TState<T>;
	constexpr int Stride = FormulaOrbitStride<F>();
	const int MaxIterations = Params.MaxIterations > 1 ? Params.MaxIterations : 1;
	const double Escape = F::ReferenceEscapeRadius(Params);
	const double Escape2 = Escape * Escape;

	TSeriesState<F> Series;
	auto Write = [&](int Index, const FState& Z, bool bSeries)
	{
		float* Point = OutFloats + 4 * Stride * Index;
		F::Pack(Z, Params, Point);
		float* Rows = Point + 4 * F::PayloadFloat4;
		if (bSeries)
		{
			Series.Store(Rows);
		}
		else
		{
			for (int I = 0; I < 4 * F::StateDim; ++I)
			{
				Rows[I] = 0.0f;
			}
		}
		// Spare slot of series row 2: Bailout^2 - |Z|^2 rounded from T precision. The GPU decides whether a pixel
		// has escaped from |Z + d|^2 - Bailout^2 = 2 Z.d + d.d - margin, which stays exact where the reference
		// itself sits next to the bailout sphere (a float |Z + d|^2 has an absolute error of ~eps Bailout^2).
		Rows[4 * 2 + 3] = ClampToFloatRange(RToDouble(T(Params.Bailout * Params.Bailout) - Dot(Z, Z)));
		if (OutOrbit)
		{
			OutOrbit[Index] = Z;
		}
	};

	Write(0, FState(), false);
	FState Z = F::Start(C);
	int Count = 1;
	for (int M = 1; M <= MaxIterations; ++M)
	{
		Write(M, Z, true);
		++Count;
		if (RToDouble(Dot(Z, Z)) > Escape2 || M == MaxIterations)
		{
			break;
		}
		Series.Advance(ToDoubleState(Z), Params, SeriesTolerance);
		Z = F::Step(Z, C, Params);
	}
	return Count;
}

// ---------------------------------------------------------------------------------------------
// High-precision distance estimator and reference ray march
// ---------------------------------------------------------------------------------------------

struct FDistanceEstimate
{
	double Distance = 0.0;   // in fractal units
	double StepLimit = std::numeric_limits<double>::infinity(); // largest step that provably cannot overshoot the set (BoundedDistanceEstimate)
	int Iterations = 0;      // n at which |z_n| > Bailout (or MaxIterations)
	bool bEscaped = false;

	/** Distance a ray may safely advance: the estimate, capped by StepLimit. Hits are still decided on Distance. */
	double SafeDistance() const { return Distance < StepLimit ? Distance : StepLimit; }
};

/** Direct (non-perturbed) DE with the orbit in T precision and the derivative in double. */
template <typename F, typename T>
inline FDistanceEstimate DistanceEstimate(const TFractalVec3<T>& Position, const FFormulaParams& Params)
{
	FDistanceEstimate Result;
	typename F::template TState<T> Z = F::Start(Position);
	double DR = 1.0;
	// Formulas with a JacobianStep track dz/dc as a matrix (see PowerMapJacobian); the others the scalar derivative.
	constexpr bool bJacobian = requires(const typename F::template TState<double>& S, double M[3][3], const FFormulaParams& P) { F::JacobianStep(S, M, P); };
	double J[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
	double R = 0.0;
	int N = 1;
	for (; N < Params.MaxIterations; ++N)
	{
		R = std::sqrt(RToDouble(Dot(Z, Z)));
		// Compared in T precision: a double |z| next to the bailout sphere would misjudge deep-zoom samples.
		if ((F::Family == EFractalFamily::EscapeTime || N > 1) && Dot(Z, Z) > T(Params.Bailout * Params.Bailout))
		{
			Result.bEscaped = true;
			break;
		}
		if constexpr (bJacobian)
		{
			F::JacobianStep(ToDoubleState(Z), J, Params);
		}
		else
		{
			DR = F::DerivativeStep(ToDoubleState(Z), DR, Params);
		}
		Z = F::Step(Z, Position, Params);
	}
	Result.Iterations = N;
	if constexpr (bJacobian)
	{
		// 0.5 r ln r / |grad r|, grad r = J^T z / r
		double Grad2 = 0.0;
		if (Result.bEscaped && R > 0.0)
		{
			const auto ZD = ToDoubleState(Z);
			for (int Col = 0; Col < 3; ++Col)
			{
				const double G = (ZD[0] * J[0][Col] + ZD[1] * J[1][Col] + ZD[2] * J[2][Col]) / R;
				Grad2 += G * G;
			}
		}
		Result.Distance = Result.bEscaped && Grad2 > 0.0 ? 0.5 * R * std::log(R) / std::sqrt(Grad2) : 0.0;
	}
	else if (F::Family == EFractalFamily::EscapeTime)
	{
		Result.Distance = Result.bEscaped ? 0.5 * R * std::log(R) / DR : 0.0;
	}
	else
	{
		if (!Result.bEscaped)
		{
			R = std::sqrt(RToDouble(Dot(Z, Z)));
		}
		Result.Distance = R / (std::fabs(DR) > 1.0e-6 ? std::fabs(DR) : 1.0e-6);
	}
	return Result;
}

/**
 * DistanceEstimate, plus the distance to the formula's bounding sphere (StepLimit). The escape-time estimate overshoots far
 * from the set, which only makes a distance used for speed or placement too large (SafeDistance caps it). Rays do not use
 * the cap: MarchReferenceRay and the GPU march clip each ray to the sphere instead, which also ends rays that miss it at
 * once. Reaching the sphere is never a hit, so Distance stays the plain estimate.
 */
template <typename F, typename T>
inline FDistanceEstimate BoundedDistanceEstimate(const TFractalVec3<T>& Position, const FFormulaParams& Params)
{
	FDistanceEstimate Result = DistanceEstimate<F, T>(Position, Params);
	if constexpr (requires { F::BoundingRadius(Params); })
	{
		const double Outside = std::sqrt(RToDouble(Dot(Position, Position))) - F::BoundingRadius(Params);
		if (Outside > 0.0)
		{
			Result.StepLimit = Outside;
		}
	}
	return Result;
}

struct FReferenceMarchSettings
{
	FFormulaParams Params;
	int MaxSteps = 300;
	double MaxDistance = 1.0e6;                // world units
	double PixelRadiusPerUnitDistance = 0.001; // hit threshold = t * this (world units)
	int InsideSearchSteps = 8;                 // after a hit, probe this many points further in for a non-escaping one
};

struct FReferenceMarchResult
{
	bool bHit = false;
	double HitDistance = 0.0;       // world units along the ray
	double ReferenceDistance = 0.0; // world units along the ray where C_ref was placed
	bool bReferenceInside = false;  // C_ref does not escape within MaxIterations
	double CameraDistanceEstimate = 0.0; // DE at the ray origin, world units
	int DirectionIndex = 0;         // which ray of MarchReferenceFan produced the result
	int Steps = 0;
};

/**
 * Marches the reference ray Origin + t * Dir (t in world units, fractal = Origin + Dir * t * Scale)
 * in T precision and returns where to put the reference point:
 *  - on a hit, a point just past the surface that does not escape (long orbit), else the hit point;
 *  - on a miss, the origin.
 */
template <typename F, typename T>
inline FReferenceMarchResult MarchReferenceRay(const TFractalVec3<T>& Origin, const double Dir[3], double Scale,
	const FReferenceMarchSettings& Settings)
{
	FReferenceMarchResult Result;
	Result.CameraDistanceEstimate = BoundedDistanceEstimate<F, T>(Origin, Settings.Params).SafeDistance() / Scale;
	double Dist = 0.0;
	double End = Settings.MaxDistance;
	if constexpr (requires { F::BoundingRadius(Settings.Params); })
	{
		// A ray that misses the bounding sphere hits nothing; one that crosses it is marched from where it enters.
		const double R = F::BoundingRadius(Settings.Params) * 1.001;
		if (R < 1.0e300)
		{
			const FDVec3 O = ToDoubleVec(Origin);
			const double C = O.X * O.X + O.Y * O.Y + O.Z * O.Z - R * R;
			if (C > 0.0)
			{
				const double B = O.X * Dir[0] + O.Y * Dir[1] + O.Z * Dir[2];
				const double Disc = B * B - C;
				if (B >= 0.0 || Disc < 0.0)
				{
					return Result;
				}
				const double Root = std::sqrt(Disc);
				Dist = (-B - Root) / Scale;
				End = End < (-B + Root) / Scale ? End : (-B + Root) / Scale;
			}
		}
	}
	for (int Step = 0; Step < Settings.MaxSteps && Dist < End; ++Step)
	{
		const TFractalVec3<T> P = OffsetDD(Origin, Dir, Dist * Scale);
		const FDistanceEstimate DE = BoundedDistanceEstimate<F, T>(P, Settings.Params);
		const double DEWorld = DE.Distance / Scale;
		Result.Steps = Step + 1;
		const double PixelRadius = Dist * Settings.PixelRadiusPerUnitDistance;
		if ((F::Family == EFractalFamily::EscapeTime && !DE.bEscaped) || DEWorld <= PixelRadius)
		{
			Result.bHit = true;
			Result.HitDistance = Dist;
			break;
		}
		Dist += DEWorld > 0.5 * PixelRadius ? DEWorld : 0.5 * PixelRadius;
	}

	if (!Result.bHit)
	{
		Result.ReferenceDistance = 0.0; // see MarchReferenceFan: without a hit the reference goes at the camera
		return Result;
	}

	Result.ReferenceDistance = Result.HitDistance;
	const double Probe = (Result.HitDistance > 0.0 ? Result.HitDistance : 1.0) * Settings.PixelRadiusPerUnitDistance;
	for (int Step = 0; Step <= Settings.InsideSearchSteps; ++Step)
	{
		const double Dist2 = Result.HitDistance + Probe * static_cast<double>(Step);
		const TFractalVec3<T> P = OffsetDD(Origin, Dir, Dist2 * Scale);
		const FDistanceEstimate DE = BoundedDistanceEstimate<F, T>(P, Settings.Params);
		if (!DE.bEscaped)
		{
			Result.ReferenceDistance = Dist2;
			Result.bReferenceInside = true;
			break;
		}
	}
	return Result;
}

/**
 * Places the reference for a whole view: marches every ray of Dirs (unit directions across the view) and keeps the
 * NEAREST hit. A pixel can only follow the reference for as many iterations as it takes to diverge from it, which for
 * formulas that cannot rebase (c only sets the start) bounds the usable distance between pixel and reference to
 * ~1e3 times the pixel's own distance from the camera, so the reference has to sit at the nearest visible surface,
 * not wherever the centre ray happens to end up (typically far away when it passes through a gap). Rays are
 * limited to the nearest hit found so far, so the cost stays close to that of one ray.
 * If nothing is hit, or the nearest hit is more than MaxHitFactor times farther than the camera's own distance
 * estimate (the samples near the camera would then be too far from the reference), the reference goes at the
 * camera: every pixel at distance t then has |dc| = Scale * t, always inside the limit above, and the camera's
 * orbit is about as long as that of a point one pixel from the surface.
 */
template <typename F, typename T>
inline FReferenceMarchResult MarchReferenceFan(const TFractalVec3<T>& Origin, const double (*Dirs)[3], int Count, double Scale,
	const FReferenceMarchSettings& Settings, double MaxHitFactor = 1000.0)
{
	FReferenceMarchResult Best;
	FReferenceMarchSettings Limited = Settings;
	for (int I = 0; I < Count; ++I)
	{
		FReferenceMarchResult R = MarchReferenceRay<F, T>(Origin, Dirs[I], Scale, Limited);
		R.DirectionIndex = I;
		if (I == 0)
		{
			Best = R;
		}
		else if (R.bHit && (!Best.bHit || R.HitDistance < Best.HitDistance))
		{
			const double CameraDE = Best.CameraDistanceEstimate;
			Best = R;
			Best.CameraDistanceEstimate = CameraDE;
		}
		if (Best.bHit)
		{
			// later rays only matter if they hit something nearer
			const double Reach = Best.HitDistance > 0.0 ? Best.HitDistance : Settings.PixelRadiusPerUnitDistance;
			Limited.MaxDistance = Reach < Settings.MaxDistance ? Reach : Settings.MaxDistance;
		}
	}
	// Camera distance floor: a camera inside the set has no meaningful estimate; use one pixel-scale unit.
	const double Reference = Best.CameraDistanceEstimate > 1.0e-3 ? Best.CameraDistanceEstimate : 1.0e-3;
	if (!Best.bHit || Best.HitDistance > MaxHitFactor * Reference)
	{
		Best.bHit = false;
		Best.ReferenceDistance = 0.0;
		Best.bReferenceInside = false;
	}
	return Best;
}

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
