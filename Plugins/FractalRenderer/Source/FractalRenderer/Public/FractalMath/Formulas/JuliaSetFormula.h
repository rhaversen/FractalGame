// Julia set of the Mandelbulb power map: z_1 = position, z_{n+1} = g_p(z_n) + c_J with the fixed
// c_J = (-0.8, 0.156, 0) (GPU: Shaders/FormulaJuliaSet.ush). The running derivative is |dz_n / dz_1|,
// i.e. dr' = p r^(p-1) dr without the "+1" of Mandelbrot-type formulas.

#pragma once

#include "PowerMap.h"

namespace FractalMath
{

struct FJuliaSetFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::JuliaSet;
	static constexpr EFractalFamily Family = EFractalFamily::EscapeTime;
	static constexpr bool bAddsC = false;
	static constexpr int StateDim = 3;
	static constexpr int PayloadFloat4 = 3;
	template <typename T>
	using TState = TFractalVec3<T>;

	// c_J rounded to float so that the CPU and the GPU iterate exactly the same set.
	static constexpr double CX = static_cast<double>(-0.8f);
	static constexpr double CY = static_cast<double>(0.156f);
	static constexpr double CZ = 0.0;

	template <typename T>
	static TState<T> Start(const TFractalVec3<T>& Position) { return Position; }

	template <typename T>
	static TState<T> Step(const TState<T>& Z, const TFractalVec3<T>& /*C*/, const FFormulaParams& Params)
	{
		return MandelbulbPower(Z, Params.Power) + TState<T>(T(CX), T(CY), T(CZ));
	}

	static double DerivativeStep(const TState<double>& Z, double DR, const FFormulaParams& Params)
	{
		return PowerMapDerivative(Z, DR, Params.Power);
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
