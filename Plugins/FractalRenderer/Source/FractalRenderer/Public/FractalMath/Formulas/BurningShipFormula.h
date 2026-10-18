// Burning Ship: z_1 = c, z_{n+1} = g_p(|z_n|) + c with |z| taken per component, dr' = p r^(p-1) dr + 1
// (r = |z| = | |z| |), DE = 0.5 r ln r / dr (GPU: Shaders/FormulaBurningShip.ush). f(0) = 0, so orbit point 0
// is a valid rebase target.
//
// GPU payload (3 float4) = PackPowerRef(|Z|) with its first three floats replaced by the SIGNED reference:
//   (Z~x, Z~y, Z~z, R) | (Rho, RPow, Phi, InvR) | (SinPTheta, CosPTheta, SinPPhi, CosPPhi)
// where Z~ is the float-rounded reference, R = |Z~|, and Rho .. CosPPhi describe the folded point |Z~|
// (float rounding is symmetric, so abs(Z~) is exactly the folded point PackPowerRef rounds).
//
// The abs fold needs no extra precise data: its only branch boundaries are the coordinate planes, and the
// distance of the reference to plane i is Z_i itself, which Z~_i carries to full float relative precision.

#pragma once

#include "PowerMap.h"

namespace FractalMath
{

struct FBurningShipFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::BurningShip;
	static constexpr EFractalFamily Family = EFractalFamily::EscapeTime;
	static constexpr bool bAddsC = true;
	static constexpr int StateDim = 3;
	static constexpr int PayloadFloat4 = 3;
	template <typename T>
	using TState = TFractalVec3<T>;

	template <typename T>
	static TState<T> Fold(const TState<T>& Z)
	{
		return TState<T>(RAbs(Z.X), RAbs(Z.Y), RAbs(Z.Z));
	}

	template <typename T>
	static TState<T> Start(const TFractalVec3<T>& Position) { return Position; }

	template <typename T>
	static TState<T> Step(const TState<T>& Z, const TFractalVec3<T>& C, const FFormulaParams& Params)
	{
		return MandelbulbPower(Fold(Z), Params.Power) + C;
	}

	static double DerivativeStep(const TState<double>& Z, double DR, const FFormulaParams& Params)
	{
		return PowerMapDerivative(Z, DR, Params.Power) + 1.0;
	}

	template <typename T>
	static void Pack(const TState<T>& Z, const FFormulaParams& Params, float* Out)
	{
		PackPowerRef(Fold(Z), Params.Power, Out);
		for (int I = 0; I < 3; ++I)
		{
			Out[I] = ClampToFloatRange(RToDouble(Z[I]));
		}
	}

	/**
	 * J = J_g(|Z|) diag(sign Z). The pixel must stay in the reference's orthant (|d| < min_i |Z_i|: inside it the
	 * fold is the isometry diag(sign Z), so |d| is also the offset entering g_p) and within the power map's own
	 * limit at |Z|. |Z| has x >= 0, so the azimuth branch cut of non-integer powers (y = 0, x < 0) is never near.
	 */
	static double SeriesStep(const TState<double>& Z, const FFormulaParams& Params, double Tolerance, double OutJ[4][4])
	{
		double Limit = 1.0e300;
		double Sign[3];
		for (int I = 0; I < 3; ++I)
		{
			Sign[I] = Z[I] < 0.0 ? -1.0 : 1.0;
			const double ToPlane = std::fabs(Z[I]);
			Limit = ToPlane < Limit ? ToPlane : Limit;
		}
		double JG[4][4] = {};
		const double PowerLimit = PowerMapSeriesStep(Fold(Z), Params.Power, Tolerance, JG);
		Limit = PowerLimit < Limit ? PowerLimit : Limit;
		for (int Row = 0; Row < 3; ++Row)
		{
			for (int Col = 0; Col < 3; ++Col)
			{
				OutJ[Row][Col] = JG[Row][Col] * Sign[Col];
			}
		}
		return Limit;
	}

	static double ReferenceEscapeRadius(const FFormulaParams& Params)
	{
		return PowerMapReferenceEscapeRadius(Params.Bailout, Params.Power);
	}

	static double BoundingRadius(const FFormulaParams& Params) { return PowerMapBoundingRadius(Params.Power, 0.0, true); }
};

} // namespace FractalMath
