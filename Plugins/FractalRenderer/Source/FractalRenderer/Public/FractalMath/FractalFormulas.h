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

} // namespace FractalMath
