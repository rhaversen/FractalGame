// Ray profile: the hybrid distance estimator along whole rays (not only near the reference) against the
// double-double truth, for references that are fresh or stale (the camera has moved on since the reference was
// built), as the game does it. Prints where the GPU distance estimate is wrong and what the shader did there.
// Usage: ray_profile <formula> <zoom> [camera distance world units] [reference lag world units] [power]
// Environment: LAB_FAN=1 places the reference like the plugin (nearest hit of rays across the view; otherwise the
// centre ray only), LAB_MARCH_ANGLE=<deg> looks that far away from the surface, LAB_MAX_BAD_FRACTION allowed share.
#include "LabCommon.h"
#include "FormulaScenes.h"
#include "FractalMath/FractalCamera.h"
#include <random>

using namespace FractalMath;

namespace FractalMath
{
inline FDVec3 ToDoubleVec(const TFractalVec3<quad>& A) { return FDVec3((double)A.X, (double)A.Y, (double)A.Z); }
}
using QV = TFractalVec3<quad>;

template <typename F>
static bool QMarch(const QV& Start, const double Dir[3], quad Tol, const FFormulaParams& P, QV& Out)
{
	auto At = [&](quad T) { return QV(Start.X + (quad)Dir[0] * T, Start.Y + (quad)Dir[1] * T, Start.Z + (quad)Dir[2] * T); };
	quad T = 0;
	for (int Step = 0; Step < 40000; Step++)
	{
		FDistanceEstimate D = DistanceEstimate<F, quad>(At(T), P);
		if (!D.bEscaped && F::Family == EFractalFamily::EscapeTime) { Out = At(T); return true; }
		if ((quad)D.Distance < Tol) { Out = At(T); return true; }
		T += (quad)D.Distance * 0.9Q;
		if (T > 40) return false;
	}
	return false;
}

template <typename F>
static int Run(FVkContext& Ctx, double Zoom, double CamDist, double Lag, double Power)
{
	const FFormulaLab Lab = GetFormulaLab(F::Type);
	FFormulaParams P;
	P.Power = Power > 0 ? Power : Lab.Power;
	P.Bailout = 10.0;
	P.MaxIterations = ComputeIterationBudget(F::Type, P.Power, 150, 10.0, Zoom * 1e-3);
	const int Index = static_cast<int>(F::Type);
	FVkKernel K(Ctx, LabSpv("ProfileMain_T" + std::to_string(Index)), "ProfileMain", {true, false, false, false});
	std::printf("##### %s power %g zoom %g, camera %g world units from the surface, reference lag %g, %d iterations\n", GetFractalName(F::Type), P.Power, Zoom, CamDist, Lag, P.MaxIterations);
	int TotalBad = 0, TotalSamples = 0;
	const double MaxBadFraction = getenv("LAB_MAX_BAD_FRACTION") ? std::atof(getenv("LAB_MAX_BAD_FRACTION")) : 0.0;
	for (const FTestLocation& Loc : Lab.Locations)
	{
		double D[3] = {Loc.Dir[0], Loc.Dir[1], Loc.Dir[2]};
		double L = std::sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
		for (double& V : D) V /= L;
		QV Surface;
		if (!QMarch<F>(QV((quad)Loc.Start[0], (quad)Loc.Start[1], (quad)Loc.Start[2]), D, (quad)(Zoom * 1e-3), P, Surface)) continue;
		// Camera CamDist world units before the surface; the reference was placed when the camera was Lag units further back.
		auto Back = [&](double World) { return FDDVec3(ToDD(Surface.X - (quad)D[0] * (quad)(World * Zoom)), ToDD(Surface.Y - (quad)D[1] * (quad)(World * Zoom)), ToDD(Surface.Z - (quad)D[2] * (quad)(World * Zoom))); };
		const FDDVec3 Cam = Back(CamDist);
		// Lag < 0: the reference marched from a random camera position |Lag| world units around the surface point
		// (the player strafed or turned since); otherwise it was marched from Lag units further back on the ray.
		FDDVec3 CamOld = Back(CamDist + Lag);
		double MarchDir[3] = {D[0], D[1], D[2]};
		if (Lag < 0)
		{
			std::mt19937_64 R2(77 + (int)(1000 * Loc.Start[0]));
			std::normal_distribution<double> G2(0, 1);
			double V[3] = {G2(R2), G2(R2), G2(R2)};
			double N = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
			CamOld = FDDVec3(ToDD(Surface.X + (quad)(V[0] / N * -Lag * Zoom)), ToDD(Surface.Y + (quad)(V[1] / N * -Lag * Zoom)), ToDD(Surface.Z + (quad)(V[2] / N * -Lag * Zoom)));
			// looking at the surface point from there
			const FDVec3 To = ToDoubleVec(FDDVec3(ToDD(Surface.X), ToDD(Surface.Y), ToDD(Surface.Z)) - CamOld);
			double M = std::sqrt(To.X * To.X + To.Y * To.Y + To.Z * To.Z);
			MarchDir[0] = To.X / M; MarchDir[1] = To.Y / M; MarchDir[2] = To.Z / M;
		}
		// LAB_MARCH_ANGLE: degrees between the direction the reference was marched in (the player's old look
		// direction) and the direction to the surface; a miss leaves the reference at the point of closest approach.
		if (const char* Ang = getenv("LAB_MARCH_ANGLE"))
		{
			const double A = std::atof(Ang) * 3.14159265358979 / 180.0;
			double Perp[3] = {-D[1], D[0], 0.0};
			double PL = std::sqrt(Perp[0] * Perp[0] + Perp[1] * Perp[1]);
			if (PL < 1e-6) { Perp[0] = 1; Perp[1] = 0; PL = 1; }
			for (double& V : Perp) V /= PL;
			double Cr[3] = {D[1] * Perp[2] - D[2] * Perp[1], D[2] * Perp[0] - D[0] * Perp[2], D[0] * Perp[1] - D[1] * Perp[0]};
			for (int C = 0; C < 3; C++) MarchDir[C] = std::cos(A) * D[C] + std::sin(A) * (Perp[C] * 0.8 + Cr[C] * 0.6);
		}
		FReferenceMarchSettings MS;
		MS.Params = P;
		MS.MaxSteps = 150;
		MS.MaxDistance = 500.0 / Zoom;
		MS.PixelRadiusPerUnitDistance = 0.0007;
		// A view around the look direction: +-45 degrees horizontally, +-31 vertically.
		double Right[3] = {MarchDir[1], -MarchDir[0], 0.0};
		double RL = std::sqrt(Right[0] * Right[0] + Right[1] * Right[1]);
		if (RL < 1e-6) { Right[0] = 1; Right[1] = 0; RL = 1; }
		for (double& V : Right) V /= RL;
		double Up[3] = {MarchDir[1] * Right[2] - MarchDir[2] * Right[1], MarchDir[2] * Right[0] - MarchDir[0] * Right[2], MarchDir[0] * Right[1] - MarchDir[1] * Right[0]};
		FFractalRayBasis View;
		for (int C = 0; C < 3; C++)
		{
			View.DirPixel00[C] = MarchDir[C] - Right[C] + 0.6 * Up[C];
			View.DirDX[C] = 2.0 / 100.0 * Right[C];
			View.DirDY[C] = -1.2 / 100.0 * Up[C];
		}
		FReferenceMarchResult RM;
		double RefDir[3] = {MarchDir[0], MarchDir[1], MarchDir[2]};
		if (getenv("LAB_FAN"))
		{
			double Fan[ReferenceFanCount][3];
			BuildReferenceFan(View, 100, 100, Fan);
			RM = MarchReferenceFan<F, FDD>(CamOld, Fan, ReferenceFanCount, Zoom, MS);
			for (int C = 0; C < 3; C++) RefDir[C] = Fan[RM.DirectionIndex][C];
		}
		else
			RM = MarchReferenceRay<F, FDD>(CamOld, MarchDir, Zoom, MS);
		const FDDVec3 CRef = OffsetDD(CamOld, RefDir, RM.ReferenceDistance * Zoom);
		const int Stride = FormulaOrbitStride<F>();
		std::vector<float> Orbit((size_t)(P.MaxIterations + 1) * Stride * 4);
		const int OrbitLen = GenerateReferenceOrbit<F, FDD>(CRef, P, Orbit.data(), nullptr, DefaultSeriesTolerance);

		// samples along a few rays from the camera (centre and tilted)
		std::mt19937_64 Rng(5);
		std::normal_distribution<double> G(0, 1);
		struct FS { double T; int Ray; float DC[3]; double Dir[3]; };
		std::vector<FS> S;
		for (int Ray = 0; Ray < 4; Ray++)
		{
			double Dr[3] = {MarchDir[0], MarchDir[1], MarchDir[2]};
			if (Ray > 0) { for (double& V : Dr) V += 0.35 * G(Rng); double N = std::sqrt(Dr[0] * Dr[0] + Dr[1] * Dr[1] + Dr[2] * Dr[2]); for (double& V : Dr) V /= N; }
			const FDVec3 Off = ToDoubleVec(Cam - CRef);
			const float CamOffset[3] = {(float)(Off.X / Zoom), (float)(Off.Y / Zoom), (float)(Off.Z / Zoom)};
			for (int I = 0; I < 300; I++)
			{
				const double T = std::pow(10.0, -2.0 + 5.5 * I / 299.0);
				FS X;
				X.T = T; X.Ray = Ray;
				for (int C = 0; C < 3; C++) { X.Dir[C] = Dr[C]; X.DC[C] = (float)Zoom * (CamOffset[C] + (float)Dr[C] * (float)T); }
				S.push_back(X);
			}
		}
		const int N = (int)S.size();
		FVkBuffer CB = Ctx.CreateBuffer(64, true), Orb = Ctx.CreateBuffer(Orbit.size() * 4), In = Ctx.CreateBuffer((size_t)N * 16), Out = Ctx.CreateBuffer((size_t)N * 16);
		float* PB = CB.As<float>();
		PB[0] = (float)Zoom; PB[1] = (float)P.Power; PB[2] = (float)P.Bailout; PB[3] = 1e-4f;
		int* IB = reinterpret_cast<int*>(PB + 4); IB[0] = P.MaxIterations; IB[1] = OrbitLen; IB[2] = N;
		PB[8] = CRef.X.ToFloat(); PB[9] = CRef.Y.ToFloat(); PB[10] = CRef.Z.ToFloat();
		std::memcpy(Orb.Mapped, Orbit.data(), Orbit.size() * 4);
		float* InF = In.As<float>();
		for (int I = 0; I < N; I++) { InF[4 * I] = S[I].DC[0]; InF[4 * I + 1] = S[I].DC[1]; InF[4 * I + 2] = S[I].DC[2]; InF[4 * I + 3] = (float)(S[I].T * 0.0007); }
		K.Dispatch({&CB, &Orb, &In, &Out}, (N + 63) / 64, 1, 1);
		const float* O = Out.As<float>();
		int Bad = 0;
		std::printf("\n== %s: reference %.3g world units ahead of the old camera (orbit %d points%s)\n", Loc.Name, RM.ReferenceDistance, OrbitLen, RM.bReferenceInside ? ", inside" : "");
		for (int I = 0; I < N; I++)
		{
			const FDDVec3 Pt = FDDVec3(Cam.X + DDTwoProd(S[I].Dir[0], S[I].T * Zoom), Cam.Y + DDTwoProd(S[I].Dir[1], S[I].T * Zoom), Cam.Z + DDTwoProd(S[I].Dir[2], S[I].T * Zoom));
			const FDistanceEstimate Tr = DistanceEstimate<F, FDD>(Pt, P);
			const double TrueW = Tr.Distance / Zoom;
			const double Pix = S[I].T * 0.0007;
			const double Err = std::fabs(O[4 * I] - TrueW) / std::max(std::min(TrueW, (double)O[4 * I]), Pix);
			if (Err > 0.25 || ((O[4 * I + 2] > 0.5f) != Tr.bEscaped && F::Family == EFractalFamily::EscapeTime))
			{
				if (Bad++ < 12)
					std::printf("  ray %d t %-10.4g |dc|/S %-10.4g true DE %-10.4g gpu DE %-10.4g  it %d/%d esc %d/%d skipped %d\n", S[I].Ray, S[I].T,
						std::sqrt((double)S[I].DC[0] * S[I].DC[0] + (double)S[I].DC[1] * S[I].DC[1] + (double)S[I].DC[2] * S[I].DC[2]) / Zoom, TrueW, O[4 * I], Tr.Iterations, (int)O[4 * I + 1], Tr.bEscaped, O[4 * I + 2] > 0.5f, (int)O[4 * I + 3]);
			}
		}
		std::printf("  -> %d of %d samples wrong by > 25%%\n", Bad, N);
		TotalBad += Bad;
		TotalSamples += N;
		for (FVkBuffer* B : {&CB, &Orb, &In, &Out}) Ctx.DestroyBuffer(*B);
	}
	std::printf("\n%s: %d of %d samples wrong (allowed %.1f%%)\n", GetFractalName(F::Type), TotalBad, TotalSamples, 100.0 * MaxBadFraction);
	return TotalBad > MaxBadFraction * TotalSamples ? 1 : 0;
}

int main(int Argc, char** Argv)
{
	if (Argc < 3) { std::printf("usage: ray_profile <formula> <zoom> [camera distance] [reference lag] [power]\n"); return 2; }
	EFractalFormula F;
	if (!ParseFormula(Argv[1], F)) return 2;
	FVkContext Ctx;
	const double Zoom = std::atof(Argv[2]);
	const double CamDist = Argc > 3 ? std::atof(Argv[3]) : 4.0;
	const double Lag = Argc > 4 ? std::atof(Argv[4]) : 0.0;
	const double Power = Argc > 5 ? std::atof(Argv[5]) : 0.0;
	const int Bad = VisitFormula(F, [&](auto Formula) { return Run<decltype(Formula)>(Ctx, Zoom, CamDist, Lag, Power); });
	return Bad ? 1 : 0;
}
