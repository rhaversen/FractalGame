// PLACEHOLDER (to be implemented): currently renders the Mandelbulb.
#pragma once
#include "MandelbulbFormula.h"
namespace FractalMath
{
struct FSierpinskiFormula : FMandelbulbFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::SierpinskiTetrahedron;
};
}
