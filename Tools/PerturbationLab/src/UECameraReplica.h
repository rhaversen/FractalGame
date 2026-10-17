// Lab-side replicas of what Unreal computes for a camera (rotation, view-to-world, reversed-Z projection) and
// the CPU version of the shader's per-pixel ray direction, used to test FractalCamera.h end to end.
#pragma once
#include "FractalMath/FractalCamera.h"
#include <cmath>

namespace FractalMath
{

/** Unnormalised world direction through pixel (PX, PY) (pixel centres are integers + 0.5). */
inline void RayBasisDirection(const FFractalRayBasis& Basis, double PX, double PY, double OutDir[3])
{
	for (int C = 0; C < 3; ++C)
	{
		OutDir[C] = Basis.DirPixel00[C] + (PX - 0.5) * Basis.DirDX[C] + (PY - 0.5) * Basis.DirDY[C];
	}
}

/** FRotationMatrix(FRotator(Pitch, Yaw, Roll)) in degrees: rows = forward (X), right (Y), up (Z). */
inline void UERotationMatrix(double PitchDeg, double YawDeg, double RollDeg, double Out[3][3])
{
	const double D2R = 3.14159265358979323846 / 180.0;
	const double SP = std::sin(PitchDeg * D2R), CP = std::cos(PitchDeg * D2R);
	const double SY = std::sin(YawDeg * D2R), CY = std::cos(YawDeg * D2R);
	const double SR = std::sin(RollDeg * D2R), CR = std::cos(RollDeg * D2R);
	Out[0][0] = CP * CY;
	Out[0][1] = CP * SY;
	Out[0][2] = SP;
	Out[1][0] = SR * SP * CY - CR * SY;
	Out[1][1] = SR * SP * SY + CR * CY;
	Out[1][2] = -SR * CP;
	Out[2][0] = -(CR * SP * CY + SR * SY);
	Out[2][1] = CY * SR - CR * SP * SY;
	Out[2][2] = CR * CP;
}

/**
 * Upper 3x3 of the engine's InvViewMatrix for a camera rotation: view = (right, up, forward), so
 * row 0 (view X) = rotation row 1, row 1 (view Y) = rotation row 2, row 2 (view Z) = rotation row 0.
 * (ViewRotationMatrix = FInverseRotationMatrix(R) * [[0,0,1],[1,0,0],[0,1,0]].)
 */
inline void UEViewToWorldFromRotation(const double Rotation[3][3], double Out[3][3])
{
	for (int C = 0; C < 3; ++C)
	{
		Out[0][C] = Rotation[1][C];
		Out[1][C] = Rotation[2][C];
		Out[2][C] = Rotation[0][C];
	}
}

/** FReversedZPerspectiveMatrix(HalfFOVX, HalfFOVX, 1, Width / Height, MinZ, MinZ) (infinite far plane). */
inline void UEReversedZPerspective(double HalfFOVXRad, double Width, double Height, double MinZ, double Out[4][4])
{
	for (int R = 0; R < 4; ++R)
	{
		for (int C = 0; C < 4; ++C)
		{
			Out[R][C] = 0.0;
		}
	}
	const double T = std::tan(HalfFOVXRad);
	Out[0][0] = 1.0 / T;
	Out[1][1] = (Width / Height) / T;
	Out[2][3] = 1.0;
	Out[3][2] = MinZ;
}

} // namespace FractalMath
