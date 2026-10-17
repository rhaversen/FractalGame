// All fractal formulas and runtime dispatch from EFractalFormula to the templated engine
// (FractalReferenceOrbit.h).

#pragma once

#include "FractalReferenceOrbit.h"
#include "Formulas/MandelbulbFormula.h"
#include "Formulas/BurningShipFormula.h"
#include "Formulas/JuliaSetFormula.h"
#include "Formulas/MandelboxFormula.h"
#include "Formulas/InvertedMengerFormula.h"
#include "Formulas/QuaternionFormula.h"
#include "Formulas/SierpinskiFormula.h"
#include "Formulas/KaleidoscopicIFSFormula.h"

namespace FractalMath
{

/** Calls Visitor(FormulaStruct{}) for the formula identified at runtime and returns its result. */
template <typename FVisitor>
inline auto VisitFormula(EFractalFormula Formula, FVisitor&& Visitor) -> decltype(Visitor(FMandelbulbFormula()))
{
	switch (Formula)
	{
	case EFractalFormula::BurningShip: return Visitor(FBurningShipFormula());
	case EFractalFormula::JuliaSet: return Visitor(FJuliaSetFormula());
	case EFractalFormula::Mandelbox: return Visitor(FMandelboxFormula());
	case EFractalFormula::InvertedMenger: return Visitor(FInvertedMengerFormula());
	case EFractalFormula::Quaternion: return Visitor(FQuaternionFormula());
	case EFractalFormula::SierpinskiTetrahedron: return Visitor(FSierpinskiFormula());
	case EFractalFormula::KaleidoscopicIFS: return Visitor(FKaleidoscopicIFSFormula());
	case EFractalFormula::Mandelbulb:
	default: return Visitor(FMandelbulbFormula());
	}
}

/** float4 per orbit point of the formula's GPU layout. */
inline int FormulaOrbitStride(EFractalFormula Formula)
{
	return VisitFormula(Formula, [](auto F) { return FormulaOrbitStride<decltype(F)>(); });
}

inline EFractalFamily FormulaFamily(EFractalFormula Formula)
{
	return VisitFormula(Formula, [](auto F) { return decltype(F)::Family; });
}

/** Reference orbit in double-double; OutFloats needs (MaxIterations + 1) * FormulaOrbitStride * 4 floats. */
inline int GenerateReferenceOrbit(EFractalFormula Formula, const FDDVec3& C, const FFormulaParams& Params, float* OutFloats,
	double SeriesTolerance = DefaultSeriesTolerance)
{
	return VisitFormula(Formula, [&](auto F)
	{
		using FFormula = decltype(F);
		return GenerateReferenceOrbit<FFormula, FDD>(C, Params, OutFloats, nullptr, SeriesTolerance);
	});
}

template <typename T>
inline FDistanceEstimate DistanceEstimate(EFractalFormula Formula, const TFractalVec3<T>& Position, const FFormulaParams& Params)
{
	return VisitFormula(Formula, [&](auto F) { return DistanceEstimate<decltype(F), T>(Position, Params); });
}

template <typename T>
inline FReferenceMarchResult MarchReferenceRay(EFractalFormula Formula, const TFractalVec3<T>& Origin, const double Dir[3], double Scale,
	const FReferenceMarchSettings& Settings)
{
	return VisitFormula(Formula, [&](auto F) { return MarchReferenceRay<decltype(F), T>(Origin, Dir, Scale, Settings); });
}

template <typename T>
inline FReferenceMarchResult MarchReferenceFan(EFractalFormula Formula, const TFractalVec3<T>& Origin, const double (*Dirs)[3], int Count, double Scale,
	const FReferenceMarchSettings& Settings)
{
	return VisitFormula(Formula, [&](auto F) { return MarchReferenceFan<decltype(F), T>(Origin, Dirs, Count, Scale, Settings); });
}

/**
 * Iterations per sample for a given pixel footprint (fractal units). A sample one footprint away from the surface
 * escapes after about ln(Bailout / footprint) / ln(growth) iterations, where growth is the per-iteration expansion:
 * the scale factor of folding fractals, the exponent of power fractals (at least 1.5: boundary orbits expand much
 * more slowly than the exponent suggests near critical points). Measured on the lab scenes the real need is up to
 * twice that, so the estimate is doubled, plus a margin. Too few iterations are not a graceful degradation: every
 * sample that does not escape counts as inside the set and the surface fills in solid (or, for folding fractals,
 * nothing is hit at all). The result is rounded up to multiples of 50 (so that the reference orbit, whose length
 * depends on it, is only rebuilt occasionally), is never below BaseIterations and is capped at 1000.
 */
inline int ComputeIterationBudget(EFractalFormula Formula, double Power, int BaseIterations, double Bailout, double FootprintFractal)
{
	const int Base = BaseIterations > 2 ? BaseIterations : 2;
	if (!(FootprintFractal > 0.0))
	{
		return Base;
	}
	double Growth = std::fabs(Power);
	if (FormulaFamily(Formula) == EFractalFamily::EscapeTime)
	{
		Growth = Growth > 1.5 ? Growth : 1.5;
	}
	else if (!(Growth > 1.01))
	{
		return Base;
	}
	const double Needed = 2.0 * std::log((Bailout > 1.0 ? Bailout : 1.0) / FootprintFractal) / std::log(Growth) + 20.0;
	const double Capped = Needed < 0.0 ? 0.0 : (Needed > 1000.0 ? 1000.0 : Needed);
	int Rounded = static_cast<int>(std::ceil(Capped / 50.0)) * 50;
	Rounded = Rounded < Base ? Base : Rounded;
	return Rounded > 1000 ? 1000 : Rounded;
}

} // namespace FractalMath
