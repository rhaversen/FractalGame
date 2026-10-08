// PLACEHOLDER (to be implemented): currently renders the Mandelbulb.
#pragma once
#include "MandelbulbFormula.h"
namespace FractalMath
{
struct FBurningShipFormula : FMandelbulbFormula
{
	static constexpr EFractalFormula Type = EFractalFormula::BurningShip;
};
}
