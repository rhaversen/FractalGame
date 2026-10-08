// Fractal formula catalogue: identifiers, display names, parameter presets and the interface every
// formula implements (CPU side; the matching GPU code lives in Shaders/Formulas/*.ush).
// Engine-free, shared by the Unreal plugin, the game module and Tools/PerturbationLab.

#pragma once

#include <cstdint>

namespace FractalMath
{

/** Order and indices must match FP_FRACTAL_TYPE in Shaders/FractalFormula.ush. */
enum class EFractalFormula : uint8_t
{
	Mandelbulb = 0,
	BurningShip = 1,
	JuliaSet = 2,
	Mandelbox = 3,
	InvertedMenger = 4,
	Quaternion = 5,
	SierpinskiTetrahedron = 6,
	KaleidoscopicIFS = 7,
};

constexpr int FractalFormulaCount = 8;

/**
 * How the distance is estimated and when escape is tested.
 *  - EscapeTime (power maps): z is tested before each step, DE = 0.5 r ln(r) / dr, 0 if z never escapes.
 *  - Folding (IFS): z is tested after each step (the starting point is not tested), DE = r / max(dr, 1e-6),
 *    also when z never escapes.
 * dr is the running derivative |dz_n / dz_0| (|dz_n / dc| plus 1 per step for formulas that add c).
 */
enum class EFractalFamily : uint8_t
{
	EscapeTime,
	Folding,
};

/** Runtime parameters shared by all formulas. Formula-specific constants are compiled into each formula. */
struct FFormulaParams
{
	double Power = 8.0;       // exponent of power maps, scale factor of folding fractals
	int MaxIterations = 150;  // iterations per sample (and reference orbit length)
	double Bailout = 10.0;    // escape radius
};

/** Interactive ranges of the two user parameters (same values as the original material renderer). */
struct FFractalPreset
{
	float MinPower;
	float MaxPower;
	float DefaultPower;
	float MinScale;     // fractal units per world unit (cm)
	float MaxScale;
	float DefaultScale;
};

inline const FFractalPreset& GetFractalPreset(EFractalFormula Formula)
{
	static const FFractalPreset Presets[FractalFormulaCount] = {
		// MinP,  MaxP,  DefP,  MinS,    MaxS,    DefS
		{1.0f, 16.0f, 8.0f, 0.0002f, 0.0020f, 0.0010f}, // Mandelbulb
		{1.5f, 16.0f, 2.0f, 0.0002f, 0.0020f, 0.0010f}, // Burning Ship
		{1.0f, 16.0f, 4.0f, 0.0002f, 0.0020f, 0.0010f}, // Julia Set
		{2.0f, 6.0f, 3.0f, 0.0020f, 0.0050f, 0.0030f},  // Mandelbox
		{2.0f, 4.0f, 2.5f, 0.0002f, 0.0020f, 0.0010f},  // Inverted Menger
		{1.0f, 25.0f, 5.0f, 0.0002f, 0.0020f, 0.0010f}, // Quaternion
		{1.5f, 5.0f, 2.0f, 0.0002f, 0.0020f, 0.0010f},  // Sierpinski Tetrahedron
		{1.3f, 2.0f, 1.7f, 0.0002f, 0.0020f, 0.0010f},  // Kaleidoscopic IFS
	};
	const int Index = static_cast<int>(Formula);
	return Presets[(Index >= 0 && Index < FractalFormulaCount) ? Index : 0];
}

inline const char* GetFractalName(EFractalFormula Formula)
{
	static const char* const Names[FractalFormulaCount] = {
		"Mandelbulb",
		"Burning Ship",
		"Julia Set",
		"Mandelbox",
		"Inverted Menger",
		"Quaternion",
		"Sierpinski Tetrahedron",
		"Kaleidoscopic IFS",
	};
	const int Index = static_cast<int>(Formula);
	return Names[(Index >= 0 && Index < FractalFormulaCount) ? Index : 0];
}

/*
 * Formula interface (see Formulas/MandelbulbFormula.h for a complete example). Each formula is a struct with
 *
 *   static constexpr EFractalFormula Type;
 *   static constexpr EFractalFamily Family;
 *   static constexpr bool bAddsC;          // z <- f(z) + c: the sample position enters every step. Such formulas
 *                                          // rebase onto orbit point 0 (z = 0, and f(0) = 0); the others switch
 *                                          // to plain float iteration once the pixel has left the reference.
 *   static constexpr int StateDim;         // 3, or 4 for quaternions
 *   static constexpr int PayloadFloat4;    // formula data per orbit point (GPU), followed by StateDim series rows
 *   template <typename T> using TState;    // TFractalVec3<T> or TFractalVec4<T>
 *
 *   template <typename T> static TState<T> Start(const TFractalVec3<T>& Position);   // z_1 (linear in Position)
 *   template <typename T> static TState<T> Step(const TState<T>& Z, const TFractalVec3<T>& C, const FFormulaParams&);
 *   static double DerivativeStep(const TState<double>& Z, double DR, const FFormulaParams&); // dr before -> after the step
 *   template <typename T> static void Pack(const TState<T>& Z, const FFormulaParams&, float* Out); // PayloadFloat4*4 floats
 *   static double SeriesStep(const TState<double>& Z, const FFormulaParams&, double Tolerance, double OutJ[4][4]);
 *       // Jacobian of z -> f(z) (without +c) at Z, and the largest |d| for which one perturbed step stays linear to
 *       // relative accuracy Tolerance and inside the branch the reference takes (0 ends the series skip).
 *   static double ReferenceEscapeRadius(const FFormulaParams&);
 */

} // namespace FractalMath
