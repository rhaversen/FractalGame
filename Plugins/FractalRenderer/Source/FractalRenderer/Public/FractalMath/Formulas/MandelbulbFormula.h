// Mandelbulb: z_1 = c, z_{n+1} = g_p(z_n) + c (GPU: Shaders/Formulas/FormulaMandelbulb.ush).

#pragma once

#include "PowerMap.h"

namespace FractalMath
{

struct FMandelbulbFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::Mandelbulb;
	static constexpr EFractalFamily Family = EFractalFamily::EscapeTime;
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
		return MandelbulbPower(Z, Params.Power) + C;
	}

	static double DerivativeStep(const TState<double>& Z, double DR, const FFormulaParams& Params)
	{
		return PowerMapDerivative(Z, DR, Params.Power) + 1.0;
	}

	template <typename T>
	static void Pack(const TState<T>& Z, const FFormulaParams& Params, float* Out)
	{
		PackPowerRef(Z, Params.Power, Out);
	}

	static double SeriesStep(const TState<double>& Z, const FFormulaParams& Params, double Tolerance, double OutJ[4][4])
	{
		return PowerMapSeriesStep(Z, Params.Power, Tolerance, OutJ);
	}

	static double ReferenceEscapeRadius(const FFormulaParams& Params)
	{
		return PowerMapReferenceEscapeRadius(Params.Bailout, Params.Power);
	}
};

} // namespace FractalMath
