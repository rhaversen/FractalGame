// Mandelbox: z_1 = c, z_{n+1} = s * SphereFold(BoxFold(z_n)) + c, dr' = |s| k dr + 1, s = Params.Power
// (GPU: Shaders/FormulaMandelbox.ush, folds in FoldMaths.h / FractalFolds.ush).
//
// GPU payload (3 float4): (Z~, r_min^2 - |B|^2) | (1 - Z, r_fixed^2 - |B|^2) | (1 + Z, |B|^2), where B is the
// box-folded reference. 1 -+ Z are rounded from double-double, so the GPU knows on which side of each box
// plane the reference lies and how far, exactly.

#pragma once

#include "FoldMaths.h"
#include "../FractalFormulaTypes.h"

namespace FractalMath
{

struct FMandelboxFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::Mandelbox;
	static constexpr EFractalFamily Family = EFractalFamily::Folding;
	static constexpr bool bAddsC = true;
	static constexpr int StateDim = 3;
	static constexpr int PayloadFloat4 = 3;
	template <typename T>
	using TState = TFractalVec3<T>;

	template <typename T>
	static TState<T> Start(const TFractalVec3<T>& Position) { return Position; }

	template <typename T>
	static TState<T> Step(const TState<T>& Z, const TFractalVec3<T>& C, const FFormulaParams& Params)
	{
		double K;
		const TState<T> S = Folds::SphereFold(Folds::BoxFold(Z), K);
		return S * Params.Power + C;
	}

	static double DerivativeStep(const TState<double>& Z, double DR, const FFormulaParams& Params)
	{
		double K;
		Folds::SphereFold(Folds::BoxFold(Z), K);
		return DR * K * std::fabs(Params.Power) + 1.0;
	}

	template <typename T>
	static void Pack(const TState<T>& Z, const FFormulaParams& /*Params*/, float* Out)
	{
		float Sphere[4];
		Folds::PackSphereFold(Folds::BoxFold(Z), Sphere);
		for (int I = 0; I < 3; ++I)
		{
			Out[I] = ClampToFloatRange(RToDouble(Z[I]));
			Out[4 + I] = ClampToFloatRange(RToDouble(T(Folds::BoxLimit) - Z[I]));
			Out[8 + I] = ClampToFloatRange(RToDouble(T(Folds::BoxLimit) + Z[I]));
		}
		Out[3] = Sphere[0];
		Out[7] = Sphere[1];
		Out[11] = Sphere[2];
	}

	static double SeriesStep(const TState<double>& Z, const FFormulaParams& Params, double Tolerance, double OutJ[4][4])
	{
		// Box fold: a reflection per component outside [-1, 1]; the pixel must stay on the reference's side of
		// every box plane.
		double J[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
		double Limit = 1.0e300;
		for (int I = 0; I < 3; ++I)
		{
			J[I][I] = (Z[I] > Folds::BoxLimit || Z[I] < -Folds::BoxLimit) ? -1.0 : 1.0;
			const double ToPlane = std::fabs(Z[I]) - Folds::BoxLimit;
			Limit = std::fabs(ToPlane) < Limit ? std::fabs(ToPlane) : Limit;
		}
		const double SphereLimit = Folds::SphereFoldSeries(Folds::BoxFold(Z), Tolerance, J);
		Limit = SphereLimit < Limit ? SphereLimit : Limit;
		for (int I = 0; I < 3; ++I)
		{
			for (int K = 0; K < 3; ++K)
			{
				OutJ[I][K] = Params.Power * J[I][K];
			}
		}
		return Limit;
	}

	static double ReferenceEscapeRadius(const FFormulaParams& Params) { return 4.0 * Params.Bailout; }
};

} // namespace FractalMath
