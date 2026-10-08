// Per-formula lab configuration: test locations for test_perturbed_de and camera scenes for render_lab.
// Each formula keeps its data in src/scenes/<Formula>.inc.
#pragma once
#include "FractalMath/FractalFormulaTypes.h"
#include <vector>

struct FTestLocation
{
	const char* Name;
	double Start[3]; // march start (fractal space)
	double Dir[3];   // march direction towards the surface
	bool bChaotic;   // numerically chaotic region: reported, not scored
};

struct FRenderScene
{
	const char* Name;
	double Start[3];
	double Dir[3];
	double Zoom;        // fractal units per world unit
	double CamDistance; // world units from the surface point
	double YawOffset;   // degrees the camera looks away from the surface point
	double PitchOffset;
	double Roll;
};

struct FFormulaLab
{
	double Power = 8.0;               // default power / scale used by the tests
	std::vector<FTestLocation> Locations;
	std::vector<FRenderScene> Scenes;
	double MaxRelErrorP50 = 3.0e-5;   // test_perturbed_de pass threshold (median relative DE error)
};

namespace LabScenes
{
#include "scenes/Mandelbulb.inc"
#include "scenes/BurningShip.inc"
#include "scenes/JuliaSet.inc"
#include "scenes/Mandelbox.inc"
#include "scenes/InvertedMenger.inc"
#include "scenes/Quaternion.inc"
#include "scenes/Sierpinski.inc"
#include "scenes/KaleidoscopicIFS.inc"
}

inline FFormulaLab GetFormulaLab(FractalMath::EFractalFormula Formula)
{
	using FractalMath::EFractalFormula;
	switch (Formula)
	{
	case EFractalFormula::BurningShip: return LabScenes::BurningShip();
	case EFractalFormula::JuliaSet: return LabScenes::JuliaSet();
	case EFractalFormula::Mandelbox: return LabScenes::Mandelbox();
	case EFractalFormula::InvertedMenger: return LabScenes::InvertedMenger();
	case EFractalFormula::Quaternion: return LabScenes::Quaternion();
	case EFractalFormula::SierpinskiTetrahedron: return LabScenes::Sierpinski();
	case EFractalFormula::KaleidoscopicIFS: return LabScenes::KaleidoscopicIFS();
	default: return LabScenes::Mandelbulb();
	}
}

/** Parses a formula from its index or a case-insensitive prefix of its name ("mandelbox", "julia", "3"). */
inline bool ParseFormula(const char* Text, FractalMath::EFractalFormula& Out)
{
	for (int I = 0; I < FractalMath::FractalFormulaCount; ++I)
	{
		const auto F = static_cast<FractalMath::EFractalFormula>(I);
		const char* Name = FractalMath::GetFractalName(F);
		bool bMatch = Text[0] != 0;
		int J = 0;
		for (; Text[J] && bMatch; ++J)
		{
			char A = Text[J], B = Name[J];
			if (A >= 'A' && A <= 'Z') A = (char)(A - 'A' + 'a');
			if (B >= 'A' && B <= 'Z') B = (char)(B - 'A' + 'a');
			bMatch = (A == B);
		}
		if (bMatch || (Text[0] - '0' == I && Text[1] == 0))
		{
			Out = F;
			return true;
		}
	}
	return false;
}
