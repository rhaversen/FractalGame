// Sierpinski Tetrahedron: z_1 = position, z_{n+1} = s * Sort(Tetra(z_n)) - (s - 1) (1, 1, 1), dr' = s dr,
// s = Params.Power. Tetra: if x+y<0 (x,y) = (-y,-x); if x+z<0 (x,z) = (-z,-x); if y+z<0 (y,z) = (-z,-y);
// Sort = descending component sort (GPU: Shaders/FormulaSierpinski.ush, folds in FoldTracker.h /
// FractalFoldTracker.ush). The position only sets z_1.
//
// GPU payload (3 float4): (Z~, reference fold code) | (X0+X1, X0+X2, X1+X2, X0-X1) | (X0-X2, X1-X2, 0, 0)
// with X = Z_m (Folds::PackSignedPermutationFold).

#pragma once

#include "FoldTracker.h"
#include "../FractalFormulaTypes.h"

namespace FractalMath
{

struct FSierpinskiFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::SierpinskiTetrahedron;
	static constexpr EFractalFamily Family = EFractalFamily::Folding;
	static constexpr bool bAddsC = false;
	static constexpr int StateDim = 3;
	static constexpr int PayloadFloat4 = 3;
	template <typename T>
	using TState = TFractalVec3<T>;

	template <typename T>
	static Folds::TSignedPermutationFold<T> Fold(const TState<T>& Z)
	{
		Folds::TSignedPermutationFold<T> Result(Z);
		Result.TetrahedralFold();
		Result.SortDescending();
		return Result;
	}

	template <typename T>
	static TState<T> Start(const TFractalVec3<T>& Position) { return Position; }

	template <typename T>
	static TState<T> Step(const TState<T>& Z, const TFractalVec3<T>& /*C*/, const FFormulaParams& Params)
	{
		const TState<T> B = Fold(Z).Value;
		const T Offset(Params.Power - 1.0);
		return TState<T>(B.X * Params.Power - Offset, B.Y * Params.Power - Offset, B.Z * Params.Power - Offset);
	}

	static double DerivativeStep(const TState<double>& /*Z*/, double DR, const FFormulaParams& Params)
	{
		return DR * Params.Power;
	}

	template <typename T>
	static void Pack(const TState<T>& Z, const FFormulaParams& /*Params*/, float* Out)
	{
		Folds::PackSignedPermutationFold(Z, Fold(Z), Out);
		Out[10] = 0.0f;
		Out[11] = 0.0f;
	}

	static double SeriesStep(const TState<double>& Z, const FFormulaParams& Params, double /*Tolerance*/, double OutJ[4][4])
	{
		// Piecewise linear: exact while the pixel stays in the reference's branch of every tetrahedral / sort test.
		const Folds::TSignedPermutationFold<double> F = Fold(Z);
		double J[3][3];
		F.Jacobian(J);
		for (int I = 0; I < 3; ++I)
		{
			for (int K = 0; K < 3; ++K)
			{
				OutJ[I][K] = Params.Power * J[I][K];
			}
		}
		return F.BranchDistance;
	}

	static double ReferenceEscapeRadius(const FFormulaParams& Params) { return 4.0 * Params.Bailout; }
};

} // namespace FractalMath
