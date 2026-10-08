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
//                           extra_1 = reference running derivative dr_m
// Point 0 is the zero state (the rebase target of formulas that add c); the orbit proper starts at m = 1.

#pragma once

#include "FractalScalar.h"
#include "FractalFormulaTypes.h"

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

/** Default tolerance of the linear series skip (validated in Tools/PerturbationLab). */
constexpr double DefaultSeriesTolerance = 1.0e-8;

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
		const double Limit = F::SeriesStep(Z, Params, Tolerance, J);
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
	int Iterations = 0;      // n at which |z_n| > Bailout (or MaxIterations)
	bool bEscaped = false;
};

/** Direct (non-perturbed) DE with the orbit in T precision and the derivative in double. */
template <typename F, typename T>
inline FDistanceEstimate DistanceEstimate(const TFractalVec3<T>& Position, const FFormulaParams& Params)
{
	FDistanceEstimate Result;
	typename F::template TState<T> Z = F::Start(Position);
	double DR = 1.0;
	double R = 0.0;
	int N = 1;
	for (; N < Params.MaxIterations; ++N)
	{
		R = std::sqrt(RToDouble(Dot(Z, Z)));
		if ((F::Family == EFractalFamily::EscapeTime || N > 1) && R > Params.Bailout)
		{
			Result.bEscaped = true;
			break;
		}
		DR = F::DerivativeStep(ToDoubleState(Z), DR, Params);
		Z = F::Step(Z, Position, Params);
	}
	Result.Iterations = N;
	if (F::Family == EFractalFamily::EscapeTime)
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
	int Steps = 0;
};

/**
 * Marches the reference ray Origin + t * Dir (t in world units, fractal = Origin + Dir * t * Scale)
 * in T precision and returns where to put the reference point:
 *  - on a hit, a point just past the surface that does not escape (long orbit), else the hit point;
 *  - on a miss, the point of closest approach (smallest DE / t ratio).
 */
template <typename F, typename T>
inline FReferenceMarchResult MarchReferenceRay(const TFractalVec3<T>& Origin, const double Dir[3], double Scale,
	const FReferenceMarchSettings& Settings)
{
	FReferenceMarchResult Result;
	double Dist = 0.0;
	double BestRatio = 1.0e300;
	double BestDist = 0.0;
	for (int Step = 0; Step < Settings.MaxSteps && Dist < Settings.MaxDistance; ++Step)
	{
		const TFractalVec3<T> P = OffsetDD(Origin, Dir, Dist * Scale);
		const FDistanceEstimate DE = DistanceEstimate<F>(P, Settings.Params);
		const double DEWorld = DE.Distance / Scale;
		if (Step == 0)
		{
			Result.CameraDistanceEstimate = DEWorld;
		}
		Result.Steps = Step + 1;
		const double PixelRadius = Dist * Settings.PixelRadiusPerUnitDistance;
		if ((F::Family == EFractalFamily::EscapeTime && !DE.bEscaped) || DEWorld <= PixelRadius)
		{
			Result.bHit = true;
			Result.HitDistance = Dist;
			break;
		}
		const double Ratio = DEWorld / (Dist > 0.0 ? Dist : 1.0e-300);
		if (Step > 0 && Ratio < BestRatio)
		{
			BestRatio = Ratio;
			BestDist = Dist;
		}
		Dist += DEWorld > 0.5 * PixelRadius ? DEWorld : 0.5 * PixelRadius;
	}

	if (!Result.bHit)
	{
		Result.ReferenceDistance = BestDist;
		return Result;
	}

	Result.ReferenceDistance = Result.HitDistance;
	const double Probe = (Result.HitDistance > 0.0 ? Result.HitDistance : 1.0) * Settings.PixelRadiusPerUnitDistance;
	for (int Step = 0; Step <= Settings.InsideSearchSteps; ++Step)
	{
		const double Dist2 = Result.HitDistance + Probe * static_cast<double>(Step);
		const TFractalVec3<T> P = OffsetDD(Origin, Dir, Dist2 * Scale);
		const FDistanceEstimate DE = DistanceEstimate<F>(P, Settings.Params);
		if (!DE.bEscaped)
		{
			Result.ReferenceDistance = Dist2;
			Result.bReferenceInside = true;
			break;
		}
	}
	return Result;
}

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
