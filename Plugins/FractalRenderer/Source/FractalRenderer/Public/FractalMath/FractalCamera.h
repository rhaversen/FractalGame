// Camera maths shared by the Unreal plugin and Tools/PerturbationLab (depends on <cmath> only).
//
// Instead of sending ClipToView / ViewToWorld matrices and unprojecting per pixel, the CPU builds an
// affine "ray basis" in double precision:
//     dir(px, py) = DirPixel00 + px * DirDX + py * DirDY      (world space, unnormalised)
// where (px, py) are integer pixel indices relative to the view rect. The shader only normalises it.
// This removes any dependence on matrix packing (row/column major) and on the reversed-Z conventions
// of the projection matrix, and it is exact for any perspective projection (including off-centre).
//
// Matrix conventions follow Unreal's FMatrix: M[Row][Col], row vectors, v' = v * M.
//   InvProjection : clip -> view   (FViewMatrices::ComputeInvProjectionNoAAMatrix())
//   ViewToWorld   : rotation rows are the world-space images of the view X (right), Y (up), Z (forward)
//                   axes, i.e. the upper 3x3 of FViewMatrices::GetInvViewMatrix().

#pragma once

#include <cmath>

namespace FractalMath
{

struct FFractalRayBasis
{
	double DirPixel00[3] = {0.0, 0.0, 1.0}; // direction through the centre of pixel (0, 0)
	double DirDX[3] = {0.0, 0.0, 0.0};      // change of the direction per pixel to the right
	double DirDY[3] = {0.0, 0.0, 0.0};      // change of the direction per pixel downwards
	double PixelRadiusPerUnitDistance = 0.0; // half the angular pixel height (world units per unit distance)
};

namespace CameraDetail
{
	/** View-space direction (scaled so that view z == 1) through normalised device coordinates. */
	inline void UnprojectNDC(const double InvProjection[4][4], double NdcX, double NdcY, double OutView[3])
	{
		// Any depth inside the frustum works; z = 1 is the near plane for Unreal's reversed-Z projection.
		const double Clip[4] = {NdcX, NdcY, 1.0, 1.0};
		double H[4];
		for (int C = 0; C < 4; ++C)
		{
			H[C] = Clip[0] * InvProjection[0][C] + Clip[1] * InvProjection[1][C] + Clip[2] * InvProjection[2][C] + Clip[3] * InvProjection[3][C];
		}
		const double InvW = 1.0 / H[3];
		const double VX = H[0] * InvW, VY = H[1] * InvW, VZ = H[2] * InvW;
		OutView[0] = VX / VZ;
		OutView[1] = VY / VZ;
		OutView[2] = 1.0;
	}

	inline void ViewToWorldDir(const double ViewToWorld[3][3], const double View[3], double OutWorld[3])
	{
		for (int C = 0; C < 3; ++C)
		{
			OutWorld[C] = View[0] * ViewToWorld[0][C] + View[1] * ViewToWorld[1][C] + View[2] * ViewToWorld[2][C];
		}
	}

	inline void PixelDirection(const double InvProjection[4][4], const double ViewToWorld[3][3], int Width, int Height, double PX, double PY, double OutWorld[3])
	{
		// Pixel coordinates grow right/down, NDC grows right/up.
		const double NdcX = PX / static_cast<double>(Width) * 2.0 - 1.0;
		const double NdcY = 1.0 - PY / static_cast<double>(Height) * 2.0;
		double View[3];
		UnprojectNDC(InvProjection, NdcX, NdcY, View);
		ViewToWorldDir(ViewToWorld, View, OutWorld);
	}
}

/**
 * Builds the per-view ray basis for a Width x Height view rect. For a perspective projection the
 * view-space direction with z == 1 is affine in the pixel coordinates, so three samples define it.
 */
inline FFractalRayBasis BuildRayBasis(const double InvProjection[4][4], const double ViewToWorld[3][3], int Width, int Height)
{
	FFractalRayBasis Basis;
	double D00[3], D10[3], D01[3];
	CameraDetail::PixelDirection(InvProjection, ViewToWorld, Width, Height, 0.5, 0.5, D00);
	CameraDetail::PixelDirection(InvProjection, ViewToWorld, Width, Height, 1.5, 0.5, D10);
	CameraDetail::PixelDirection(InvProjection, ViewToWorld, Width, Height, 0.5, 1.5, D01);
	double LenDY2 = 0.0;
	for (int C = 0; C < 3; ++C)
	{
		Basis.DirPixel00[C] = D00[C];
		Basis.DirDX[C] = D10[C] - D00[C];
		Basis.DirDY[C] = D01[C] - D00[C];
		LenDY2 += Basis.DirDY[C] * Basis.DirDY[C];
	}
	// |DirDY| is the pixel height at unit forward distance (the forward component of every direction is 1).
	Basis.PixelRadiusPerUnitDistance = 0.5 * std::sqrt(LenDY2);
	return Basis;
}

/** General 4x4 inverse (cofactor expansion), as FMatrix::Inverse(). Returns false if singular. */
inline bool InvertMatrix4(const double M[4][4], double Out[4][4])
{
	const double* A = &M[0][0];
	double Inv[16];
	Inv[0] = A[5] * A[10] * A[15] - A[5] * A[11] * A[14] - A[9] * A[6] * A[15] + A[9] * A[7] * A[14] + A[13] * A[6] * A[11] - A[13] * A[7] * A[10];
	Inv[4] = -A[4] * A[10] * A[15] + A[4] * A[11] * A[14] + A[8] * A[6] * A[15] - A[8] * A[7] * A[14] - A[12] * A[6] * A[11] + A[12] * A[7] * A[10];
	Inv[8] = A[4] * A[9] * A[15] - A[4] * A[11] * A[13] - A[8] * A[5] * A[15] + A[8] * A[7] * A[13] + A[12] * A[5] * A[11] - A[12] * A[7] * A[9];
	Inv[12] = -A[4] * A[9] * A[14] + A[4] * A[10] * A[13] + A[8] * A[5] * A[14] - A[8] * A[6] * A[13] - A[12] * A[5] * A[10] + A[12] * A[6] * A[9];
	Inv[1] = -A[1] * A[10] * A[15] + A[1] * A[11] * A[14] + A[9] * A[2] * A[15] - A[9] * A[3] * A[14] - A[13] * A[2] * A[11] + A[13] * A[3] * A[10];
	Inv[5] = A[0] * A[10] * A[15] - A[0] * A[11] * A[14] - A[8] * A[2] * A[15] + A[8] * A[3] * A[14] + A[12] * A[2] * A[11] - A[12] * A[3] * A[10];
	Inv[9] = -A[0] * A[9] * A[15] + A[0] * A[11] * A[13] + A[8] * A[1] * A[15] - A[8] * A[3] * A[13] - A[12] * A[1] * A[11] + A[12] * A[3] * A[9];
	Inv[13] = A[0] * A[9] * A[14] - A[0] * A[10] * A[13] - A[8] * A[1] * A[14] + A[8] * A[2] * A[13] + A[12] * A[1] * A[10] - A[12] * A[2] * A[9];
	Inv[2] = A[1] * A[6] * A[15] - A[1] * A[7] * A[14] - A[5] * A[2] * A[15] + A[5] * A[3] * A[14] + A[13] * A[2] * A[7] - A[13] * A[3] * A[6];
	Inv[6] = -A[0] * A[6] * A[15] + A[0] * A[7] * A[14] + A[4] * A[2] * A[15] - A[4] * A[3] * A[14] - A[12] * A[2] * A[7] + A[12] * A[3] * A[6];
	Inv[10] = A[0] * A[5] * A[15] - A[0] * A[7] * A[13] - A[4] * A[1] * A[15] + A[4] * A[3] * A[13] + A[12] * A[1] * A[7] - A[12] * A[3] * A[5];
	Inv[14] = -A[0] * A[5] * A[14] + A[0] * A[6] * A[13] + A[4] * A[1] * A[14] - A[4] * A[2] * A[13] - A[12] * A[1] * A[6] + A[12] * A[2] * A[5];
	Inv[3] = -A[1] * A[6] * A[11] + A[1] * A[7] * A[10] + A[5] * A[2] * A[11] - A[5] * A[3] * A[10] - A[9] * A[2] * A[7] + A[9] * A[3] * A[6];
	Inv[7] = A[0] * A[6] * A[11] - A[0] * A[7] * A[10] - A[4] * A[2] * A[11] + A[4] * A[3] * A[10] + A[8] * A[2] * A[7] - A[8] * A[3] * A[6];
	Inv[11] = -A[0] * A[5] * A[11] + A[0] * A[7] * A[9] + A[4] * A[1] * A[11] - A[4] * A[3] * A[9] - A[8] * A[1] * A[7] + A[8] * A[3] * A[5];
	Inv[15] = A[0] * A[5] * A[10] - A[0] * A[6] * A[9] - A[4] * A[1] * A[10] + A[4] * A[2] * A[9] + A[8] * A[1] * A[6] - A[8] * A[2] * A[5];
	const double Det = A[0] * Inv[0] + A[1] * Inv[4] + A[2] * Inv[8] + A[3] * Inv[12];
	if (Det == 0.0)
	{
		return false;
	}
	const double InvDet = 1.0 / Det;
	for (int I = 0; I < 16; ++I)
	{
		(&Out[0][0])[I] = Inv[I] * InvDet;
	}
	return true;
}

} // namespace FractalMath
