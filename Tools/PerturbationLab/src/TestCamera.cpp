// Verifies the camera maths that turns Unreal's view matrices into per-pixel world ray directions.
#include "LabCommon.h"
#include "UECameraReplica.h"
#include <random>

using namespace FractalMath;
static int Failures = 0;

static void Check(const char* Name, double Value, double Tol)
{
	bool Ok = Value <= Tol;
	if (!Ok) Failures++;
	std::printf("  %-66s %.3e (tol %.0e) %s\n", Name, Value, Tol, Ok ? "OK" : "FAIL");
}

static void Normalize(double V[3]) { double L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]); for (int I = 0; I < 3; I++) V[I] /= L; }
static double Dot3(const double A[3], const double B[3]) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
static double AngleBetween(const double A[3], const double B[3])
{
	double X[3] = {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
	return std::atan2(std::sqrt(Dot3(X, X)), Dot3(A, B));
}

/** The previous shader's GetCameraRay(), evaluated on the CPU (row-vector mul as in UE HLSL). */
static void OldShaderRay(const double ClipToView[4][4], const double ViewToWorld4[4][4], double PX, double PY, int W, int H, double Out[3])
{
	double Ndc[2] = {PX / W * 2.0 - 1.0, PY / H * 2.0 - 1.0};
	Ndc[1] = -Ndc[1];
	double Clip[4] = {Ndc[0], Ndc[1], 1.0, 1.0}, View[4];
	for (int C = 0; C < 4; C++) View[C] = Clip[0] * ClipToView[0][C] + Clip[1] * ClipToView[1][C] + Clip[2] * ClipToView[2][C] + Clip[3] * ClipToView[3][C];
	for (int C = 0; C < 3; C++) View[C] /= View[3];
	Normalize(View);
	for (int C = 0; C < 3; C++) Out[C] = View[0] * ViewToWorld4[0][C] + View[1] * ViewToWorld4[1][C] + View[2] * ViewToWorld4[2][C];
	Normalize(Out);
}

int main()
{
	FVkContext Ctx;
	std::printf("[1] HLSL matrix convention (cbuffer FMatrix44f uploaded verbatim, DXC -Zpr like Unreal)\n");
	{
		FVkKernel K(Ctx, LabSpv("MatrixMain"), "MatrixMain", {true, false});
		FVkBuffer CB = Ctx.CreateBuffer(80, true), Out = Ctx.CreateBuffer(64);
		float* M = CB.As<float>();
		double MD[4][4];
		for (int R = 0; R < 4; R++) for (int C = 0; C < 4; C++) { MD[R][C] = 1 + R * 4 + C + 0.25 * R * C; M[R * 4 + C] = (float)MD[R][C]; } // row-major memory, as FMatrix44f
		double V[4] = {0.5, -1.25, 2.0, 1.0};
		for (int I = 0; I < 4; I++) M[16 + I] = (float)V[I];
		K.Dispatch({&CB, &Out}, 1, 1, 1);
		float* R = Out.As<float>();
		double Err = 0;
		for (int C = 0; C < 4; C++)
		{
			double Want = V[0] * MD[0][C] + V[1] * MD[1][C] + V[2] * MD[2][C] + V[3] * MD[3][C];
			Err = std::max(Err, std::fabs(R[C] - Want));
		}
		Check("mul(v, M) == v * M (row-vector convention)", Err, 1e-4);
		Check("M[0][1] addresses row 0, column 1", std::fabs(R[4] - MD[0][1]) + std::fabs(R[5] - MD[1][0]) + std::fabs(R[6] - MD[3][0]) + std::fabs(R[7] - MD[0][3]), 1e-6);
		Ctx.DestroyBuffer(CB);
		Ctx.DestroyBuffer(Out);
	}

	std::printf("\n[2] Ray basis from Unreal view matrices (random cameras, FOVs, aspect ratios)\n");
	std::mt19937_64 Rng(3);
	std::uniform_real_distribution<double> U(0, 1);
	double MaxCenter = 0, MaxFovErr = 0, MaxAffine = 0, MaxOld = 0, MaxRadius = 0, MaxNormalRight = 0;
	int Orientation = 0, NCams = 0;
	for (int Cam = 0; Cam < 500; Cam++)
	{
		double Pitch = -89 + 178 * U(Rng), Yaw = -180 + 360 * U(Rng), Roll = -180 + 360 * U(Rng);
		double HalfFov = (20 + 70 * U(Rng)) * 3.14159265358979323846 / 360.0; // horizontal FOV 20..90 deg
		int W = 64 + (int)(1900 * U(Rng)), H = 64 + (int)(1000 * U(Rng));
		double Rot[3][3], V2W[3][3], Proj[4][4], InvProj[4][4];
		UERotationMatrix(Pitch, Yaw, Roll, Rot);
		UEViewToWorldFromRotation(Rot, V2W);
		UEReversedZPerspective(HalfFov, W, H, 10.0, Proj);
		InvertMatrix4(Proj, InvProj);
		FFractalRayBasis B = BuildRayBasis(InvProj, V2W, W, H);
		NCams++;

		// centre of the view rect looks along the camera forward axis (rotation row 0)
		double C[3];
		RayBasisDirection(B, W * 0.5, H * 0.5, C);
		Normalize(C);
		MaxCenter = std::max(MaxCenter, AngleBetween(C, Rot[0]));

		// left/right edges span exactly the horizontal FOV, and the vertical FOV follows the aspect ratio
		double L[3], R[3], T[3], Bt[3];
		RayBasisDirection(B, 0, H * 0.5, L);
		RayBasisDirection(B, W, H * 0.5, R);
		RayBasisDirection(B, W * 0.5, 0, T);
		RayBasisDirection(B, W * 0.5, H, Bt);
		Normalize(L); Normalize(R); Normalize(T); Normalize(Bt);
		double VFov = 2 * std::atan(std::tan(HalfFov) * H / W);
		MaxFovErr = std::max(MaxFovErr, std::fabs(AngleBetween(L, R) - 2 * HalfFov));
		MaxFovErr = std::max(MaxFovErr, std::fabs(AngleBetween(T, Bt) - VFov));

		// orientation: pixel (0,0) is top-left => left of forward (-right) and above (+up)
		double TL[3];
		RayBasisDirection(B, 0.5, 0.5, TL);
		if (Dot3(TL, Rot[1]) < 0 && Dot3(TL, Rot[2]) > 0) Orientation++;
		// moving right in pixels moves the ray along the camera's right axis only
		MaxNormalRight = std::max(MaxNormalRight, std::fabs(Dot3(B.DirDX, Rot[2])) + std::fabs(Dot3(B.DirDX, Rot[0])));

		// affine basis == per-pixel unprojection, and == the previous shader's GetCameraRay()
		double InvView4[4][4] = {};
		for (int I = 0; I < 3; I++) for (int J = 0; J < 3; J++) InvView4[I][J] = V2W[I][J];
		InvView4[3][3] = 1;
		for (int S = 0; S < 8; S++)
		{
			double PX = U(Rng) * W, PY = U(Rng) * H, A[3], Ref[3], Old[3];
			RayBasisDirection(B, PX, PY, A);
			Normalize(A);
			CameraDetail::PixelDirection(InvProj, V2W, W, H, PX, PY, Ref);
			Normalize(Ref);
			OldShaderRay(InvProj, InvView4, PX, PY, W, H, Old);
			MaxAffine = std::max(MaxAffine, AngleBetween(A, Ref));
			MaxOld = std::max(MaxOld, AngleBetween(A, Old));
		}
		// pixel radius == half the angular pixel height at the centre
		double Above[3];
		RayBasisDirection(B, W * 0.5, H * 0.5 + 1.0, Above);
		Normalize(Above);
		MaxRadius = std::max(MaxRadius, std::fabs(0.5 * AngleBetween(C, Above) - B.PixelRadiusPerUnitDistance) / B.PixelRadiusPerUnitDistance);
	}
	Check("view centre ray == camera forward axis (rad)", MaxCenter, 1e-12);
	Check("edge-to-edge angles == horizontal / vertical FOV (rad)", MaxFovErr, 1e-12);
	Check("pixel (0,0) is top-left (fraction of cameras failing)", 1.0 - (double)Orientation / NCams, 0);
	Check("+x pixel step has no up/forward component", MaxNormalRight, 1e-12);
	Check("affine ray basis == per-pixel unprojection (rad)", MaxAffine, 1e-12);
	Check("ray basis == previous shader GetCameraRay (rad)", MaxOld, 1e-12);
	Check("PixelRadiusPerUnitDistance == half pixel angle (small-angle approx.)", MaxRadius, 1e-3);

	std::printf("\n[3] Float upload of the basis: worst angular error vs double, relative to the pixel size\n");
	{
		double Rot[3][3], V2W[3][3], Proj[4][4], InvProj[4][4];
		UERotationMatrix(31.7, -122.4, 17.0, Rot);
		UEViewToWorldFromRotation(Rot, V2W);
		const int W = 3840, H = 2160;
		UEReversedZPerspective(45.0 * 3.14159265358979323846 / 180.0, W, H, 10.0, Proj);
		InvertMatrix4(Proj, InvProj);
		FFractalRayBasis B = BuildRayBasis(InvProj, V2W, W, H);
		float F00[3], FDX[3], FDY[3];
		for (int I = 0; I < 3; I++) { F00[I] = (float)B.DirPixel00[I]; FDX[I] = (float)B.DirDX[I]; FDY[I] = (float)B.DirDY[I]; }
		double Worst = 0;
		for (int PY = 0; PY < H; PY += 37)
			for (int PX = 0; PX < W; PX += 41)
			{
				double D[3], F[3];
				RayBasisDirection(B, PX + 0.5, PY + 0.5, D);
				Normalize(D);
				for (int I = 0; I < 3; I++) F[I] = (double)((float)(F00[I] + (float)PX * FDX[I] + (float)PY * FDY[I]));
				Normalize(F);
				Worst = std::max(Worst, AngleBetween(D, F));
			}
		Check("4K view: float ray direction error / pixel angle", Worst / (2 * B.PixelRadiusPerUnitDistance), 1e-2);
	}
	std::printf("\n%s\n", Failures ? "CAMERA TESTS FAILED" : "ALL CAMERA TESTS PASSED");
	return Failures ? 1 : 0;
}
