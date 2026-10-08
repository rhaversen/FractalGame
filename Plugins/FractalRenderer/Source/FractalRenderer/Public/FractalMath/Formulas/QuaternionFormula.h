// PLACEHOLDER (to be implemented): currently renders the Mandelbulb.
#pragma once
#include "MandelbulbFormula.h"
namespace FractalMath
{
struct FQuaternionFormula : FMandelbulbFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::Quaternion;
};
}
