// Kaleidoscopic IFS: z_1 = c, z_{n+1} = s * SphereFold(Sort(|z_n|)) + c, dr' = |s| k dr + 1, s = Params.Power,
// Sort = descending component sort, sphere fold r_min 0.5 / r_fixed 1 with factor k (GPU:
// Shaders/FormulaKaleidoscopicIFS.ush, folds in FoldTracker.h / FractalFoldTracker.ush and FoldMaths.h /
// FractalFolds.ush).
//
// GPU payload (3 float4): (Z~, reference fold code) | (X0+X1, X0+X2, X1+X2, X0-X1) | (X0-X2, X1-X2,
// r_min^2 - |B|^2, r_fixed^2 - |B|^2) with X = Z_m and B = Sort(|Z_m|), the vector entering the sphere fold.
// |B|^2 = |Z_m|^2 is only needed to relative accuracy, the GPU forms it from Z~.

#pragma once

#include "FoldMaths.h"
#include "FoldTracker.h"
#include "../FractalFormulaTypes.h"

namespace FractalMath
{

struct FKaleidoscopicIFSFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::KaleidoscopicIFS;
	static constexpr EFractalFamily Family = EFractalFamily::Folding;
	static constexpr bool bAddsC = true;
	static constexpr int StateDim = 3;
	static constexpr int PayloadFloat4 = 3;
	template <typename T>
	using TState = TFractalVec3<T>;

	template <typename T>
	static Folds::TSignedPermutationFold<T> Fold(const TState<T>& Z)
	{
		Folds::TSignedPermutationFold<T> Result(Z);
		Result.Abs();
		Result.SortDescending();
		return Result;
	}

	template <typename T>
	static TState<T> Start(const TFractalVec3<T>& Position) { return Position; }

	template <typename T>
	static TState<T> Step(const TState<T>& Z, const TFractalVec3<T>& C, const FFormulaParams& Params)
	{
		double K;
		const TState<T> S = Folds::SphereFold(Fold(Z).Value, K);
		return S * Params.Power + C;
	}

	static double DerivativeStep(const TState<double>& Z, double DR, const FFormulaParams& Params)
	{
		double K;
		Folds::SphereFold(Fold(Z).Value, K);
		return DR * K * std::fabs(Params.Power) + 1.0;
	}

	template <typename T>
	static void Pack(const TState<T>& Z, const FFormulaParams& /*Params*/, float* Out)
	{
		const Folds::TSignedPermutationFold<T> F = Fold(Z);
		Folds::PackSignedPermutationFold(Z, F, Out);
		float Sphere[4];
		Folds::PackSphereFold(F.Value, Sphere);
		Out[10] = Sphere[0];
		Out[11] = Sphere[1];
	}

	static double SeriesStep(const TState<double>& Z, const FFormulaParams& Params, double Tolerance, double OutJ[4][4])
	{
		const Folds::TSignedPermutationFold<double> F = Fold(Z);
		double J[3][3];
		F.Jacobian(J);
		const double SphereLimit = Folds::SphereFoldSeries(F.Value, Tolerance, J);
		for (int I = 0; I < 3; ++I)
		{
			for (int K = 0; K < 3; ++K)
			{
				OutJ[I][K] = Params.Power * J[I][K];
			}
		}
		return SphereLimit < F.BranchDistance ? SphereLimit : F.BranchDistance;
	}

	static double ReferenceEscapeRadius(const FFormulaParams& Params) { return 4.0 * Params.Bailout; }
};

} // namespace FractalMath
