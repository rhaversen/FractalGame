// End-to-end precision test of the perturbed distance estimator across zoom depths.
//
// For several points on the power-8 Mandelbulb surface (found by marching in quad precision) and zoom
// scales S = 1e-3 ... 1e-29, sample points c = C_ref + dc with |dc| ~ S around the surface and compare
//   GPU perturbed DE   (FractalPerturbation.ush, float deltas, double-double reference orbit)
//   GPU naive DE       (plain float iteration at float(C_ref) + dc)
// against a __float128 direct iteration at the exact point.
#include "LabCommon.h"
#include <random>
#include <thread>
#include <atomic>
#include <cstdlib>

using namespace FractalMath;

namespace FractalMath
{
inline FDVec3 ToDoubleVec(const TFractalVec3<quad>& A) { return FDVec3((double)A.X, (double)A.Y, (double)A.Z); }
}
using QV = TFractalVec3<quad>;

static const double Power = 8.0;
static const int MaxIter = 200;
static const double Bailout = 10.0;
static int Failures = 0;

static FDistanceEstimate QDE(const QV& C) { return DistanceEstimate(C, Power, MaxIter, Bailout); }

/**
 * March from Start along Dir (unit) in quad until DE < Tol; returns the last (outside) point. If a step
 * lands inside the set (the DE is only an estimate), bisect between the last outside and the inside point.
 */
static bool QMarchToSurface(const QV& Start, const double Dir[3], quad Tol, QV& OutOutside)
{
	auto At = [&](quad T) { return QV(Start.X + (quad)Dir[0] * T, Start.Y + (quad)Dir[1] * T, Start.Z + (quad)Dir[2] * T); };
	quad T = 0;
	for (int Step = 0; Step < 20000; Step++)
	{
		FDistanceEstimate D = QDE(At(T));
		if (!D.bEscaped)
		{
			if (Step == 0) return false;
			quad Lo = T - (quad)0, Hi = T;
			Lo = T; // find an outside point behind
			quad Back = Tol;
			while (!QDE(At(Hi - Back)).bEscaped && Back < 1) Back *= 2;
			Lo = Hi - Back;
			for (int K = 0; K < 200 && Hi - Lo > Tol * 0.1Q; K++)
			{
				quad Mid = 0.5Q * (Lo + Hi);
				FDistanceEstimate DM = QDE(At(Mid));
				if (DM.bEscaped) { Lo = Mid; if ((quad)DM.Distance < Tol) break; }
				else Hi = Mid;
			}
			OutOutside = At(Lo);
			return true;
		}
		if ((quad)D.Distance < Tol) { OutOutside = At(T); return true; }
		T += (quad)D.Distance * 0.9Q;
	}
	return false;
}

struct FDepthStats
{
	FStats PertRel, NaiveRel, PertAbs, NaiveAbs, PertMarch;
	int N = 0, IterMatch = 0, IterOff1 = 0, EscMismatch = 0, NaiveEscMismatch = 0, NaiveIterMatch = 0;
	int Glitch = 0;
	int Chaotic = 0;
	int NaiveGlitch = 0;
	double MaxRebases = 0, MeanRebases = 0;
};

static FDepthStats RunDepth(FVkContext& Ctx, FVkKernel& K, const FDDVec3& CRef, double S, int NSamples, uint64_t Seed, bool bVerbose)
{
	FDepthStats St;
	// reference orbit (production path)
	const double RefEscape = ReferenceEscapeRadius(Bailout, Power);
	std::vector<FOrbitPointGPU> Orbit(MaxIter + 1);
	const double Tol = getenv("LAB_SERIES_TOL") ? std::atof(getenv("LAB_SERIES_TOL")) : DefaultSeriesTolerance;
	const int OrbitLen = GenerateReferenceOrbit(CRef, Power, MaxIter, RefEscape, Orbit.data(), (FDDVec3*)nullptr, Bailout, Tol);

	std::mt19937_64 Rng(Seed);
	std::normal_distribution<double> G(0, 1);
	std::uniform_real_distribution<double> U(0, 1);
	std::vector<float> DC(NSamples * 4);
	for (int I = 0; I < NSamples; I++)
	{
		double V[3] = {G(Rng), G(Rng), G(Rng)};
		double L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
		double Rad = 4.0 * std::cbrt(U(Rng)) * S;
		for (int J = 0; J < 3; J++) DC[4 * I + J] = (float)(V[J] / L * Rad);
		DC[4 * I + 3] = 0;
	}

	FVkBuffer CB = Ctx.CreateBuffer(sizeof(FLabParams), true);
	FVkBuffer In = Ctx.CreateBuffer(DC.size() * 4), Orb = Ctx.CreateBuffer(Orbit.size() * sizeof(FOrbitPointGPU));
	FVkBuffer OA = Ctx.CreateBuffer((size_t)NSamples * 16), OB = Ctx.CreateBuffer((size_t)NSamples * 16);
	FLabParams* P = CB.As<FLabParams>();
	P->Power = (float)Power; P->Scale = (float)S; P->Bailout = (float)Bailout; P->ConvergenceEpsilon = 0;
	P->Count = NSamples; P->OrbitLength = OrbitLen; P->MaxIterations = MaxIter; P->MinIterations = 0;
	P->RefX = CRef.X.ToFloat(); P->RefY = CRef.Y.ToFloat(); P->RefZ = CRef.Z.ToFloat();
	std::memcpy(In.Mapped, DC.data(), DC.size() * 4);
	std::memcpy(Orb.Mapped, Orbit.data(), Orbit.size() * sizeof(FOrbitPointGPU));
	K.Dispatch({&CB, &In, &Orb, &OA, &OB}, (NSamples + 63) / 64, 1, 1);
	std::vector<float> RA(OA.As<float>(), OA.As<float>() + NSamples * 4), RB(OB.As<float>(), OB.As<float>() + NSamples * 4);
	for (FVkBuffer* B : {&CB, &In, &Orb, &OA, &OB}) Ctx.DestroyBuffer(*B);

	// ground truth in quad, multithreaded
	std::vector<FDistanceEstimate> Truth(NSamples), DDBase(NSamples);
	std::atomic<int> Next(0);
	const QV QRef(ToQuad(CRef.X), ToQuad(CRef.Y), ToQuad(CRef.Z));
	auto Worker = [&]() {
		for (int I = Next++; I < NSamples; I = Next++)
		{
			QV C(QRef.X + (quad)DC[4 * I], QRef.Y + (quad)DC[4 * I + 1], QRef.Z + (quad)DC[4 * I + 2]);
			Truth[I] = QDE(C);
			// double-double direct iteration as a conditioning probe: if even dd and quad disagree, the
			// point is numerically chaotic (no finite precision gives a reproducible answer).
			FDDVec3 CD(CRef.X + (double)DC[4 * I], CRef.Y + (double)DC[4 * I + 1], CRef.Z + (double)DC[4 * I + 2]);
			DDBase[I] = DistanceEstimate(CD, Power, MaxIter, Bailout);
		}
	};
	std::vector<std::thread> Threads;
	for (int T = 0; T < 4; T++) Threads.emplace_back(Worker);
	for (auto& T : Threads) T.join();

	for (int I = 0; I < NSamples; I++)
	{
		const FDistanceEstimate& T = Truth[I];
		float DEp = RA[4 * I], Itp = RA[4 * I + 1];
		bool Escp = RA[4 * I + 2] > 0.5f;
		float DEn = RB[4 * I], Itn = RB[4 * I + 1];
		bool Escn = RB[4 * I + 2] > 0.5f;
		St.N++;
		St.MeanRebases += RA[4 * I + 3];
		St.MaxRebases = std::max(St.MaxRebases, (double)RA[4 * I + 3]);
		if (Escp != T.bEscaped) St.EscMismatch++;
		if (Escn != T.bEscaped) St.NaiveEscMismatch++;
		if ((int)Itp == T.Iterations) St.IterMatch++;
		else if (std::abs((int)Itp - T.Iterations) == 1) St.IterOff1++;
		if ((int)Itn == T.Iterations) St.NaiveIterMatch++;
		// Errors in world units (S fractal units = 1 world unit; samples lie within 4 units). What a ray
		// marcher needs is absolute accuracy relative to the pixel footprint (~1e-3 units at distance 1).
		double TrueWorld = T.bEscaped ? T.Distance / S : 0.0;
		double Ap = std::fabs(DEp - TrueWorld), An = std::fabs(DEn - TrueWorld);
		St.PertAbs.Add(Ap);
		St.NaiveAbs.Add(An);
		if (T.bEscaped && TrueWorld > 1e-2)
		{
			St.PertRel.Add(Ap / TrueWorld);
			St.NaiveRel.Add(An / TrueWorld);
		}
		double DDWorld = DDBase[I].bEscaped ? DDBase[I].Distance / S : 0.0;
		bool bConditioned = std::fabs(DDWorld - TrueWorld) < std::max(1e-5, 1e-4 * TrueWorld);
		if (!bConditioned) { St.Chaotic++; }
		// Marching-relevant error: relative to the step (DE) far from the surface, relative to the pixel
		// footprint (~1e-3 world units at distance 1) close to it. A glitch is a 10% error in either.
		double MarchErr = Ap / std::max(TrueWorld, 1e-3);
		St.PertMarch.Add(MarchErr);
		if (MarchErr > 0.1 && bConditioned) St.Glitch++;
		if (An / std::max(TrueWorld, 1e-3) > 0.1 && bConditioned) St.NaiveGlitch++;
		if (bVerbose && MarchErr > 0.1 && bConditioned)
		{
			FDVec3 CD(CRef.X.ToDouble() + DC[4 * I], CRef.Y.ToDouble() + DC[4 * I + 1], CRef.Z.ToDouble() + DC[4 * I + 2]);
			FDistanceEstimate DD = DistanceEstimate(CD, Power, MaxIter, Bailout);
			std::printf("      c=(%.9e, %.9e, %.12f) double: DE %.6e it %d | naive gpu: DE %.6e it %d\n", CD.X, CD.Y, CD.Z, DD.Distance / S, DD.Iterations, DEn, (int)Itn);
		}
		if (bVerbose && MarchErr > 0.1 && bConditioned)
			std::printf("      glitch sample %d: |dc|/S %.3f true DE %.6e (it %d) gpu %.6e (it %d, rebases %d)\n", I,
				std::sqrt((double)DC[4 * I] * DC[4 * I] + (double)DC[4 * I + 1] * DC[4 * I + 1] + (double)DC[4 * I + 2] * DC[4 * I + 2]) / S,
				TrueWorld, T.Iterations, DEp, (int)Itp, (int)RA[4 * I + 3]);
	}
	St.MeanRebases /= std::max(1, St.N);
	return St;
}

int main(int Argc, char** Argv)
{
	FVkContext Ctx;
	FVkKernel K(Ctx, LabSpv("DEMain"), "DEMain", {true, false, false, false, false});
	std::printf("device: %s\n", Ctx.DeviceName.c_str());
	const int NSamples = (Argc > 1) ? std::atoi(Argv[1]) : 1500;

	struct FLoc { const char* Name; double Start[3]; double Dir[3]; bool bChaotic; };
	FLoc Locs[] = {
		{"equator-ish", {2.0, 0.3, 0.2}, {-1, -0.12, -0.07}, false},
		{"upper lobe", {0.6, 0.8, 1.6}, {-0.3, -0.45, -0.85}, false},
		{"diagonal", {-1.3, 1.2, -1.1}, {0.6, -0.55, 0.5}, false},
		{"lower lobe", {0.3, -1.6, -1.5}, {-0.15, 0.7, 0.7}, false},
		{"+z pole (numerically chaotic, not scored)", {1e-9, 2e-9, 2.0}, {0, 0, -1}, true},
	};
	const double Depths[] = {1e-3, 1e-5, 1e-7, 1e-9, 1e-11, 1e-13, 1e-15, 1e-17, 1e-19, 1e-21, 1e-23, 1e-25, 1e-27, 1e-29};

	const bool bVerbose = getenv("LAB_VERBOSE") != nullptr;
	for (const FLoc& L : Locs)
	{
		double D[3] = {L.Dir[0], L.Dir[1], L.Dir[2]};
		double Len = std::sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
		for (double& V : D) V /= Len;
		QV Start((quad)L.Start[0], (quad)L.Start[1], (quad)L.Start[2]);
		QV Surface;
		if (!QMarchToSurface(Start, D, 1e-34Q, Surface))
		{
			std::printf("\n%s: march failed\n", L.Name);
			Failures++;
			continue;
		}
		for (int RefMode = 0; RefMode < 2; RefMode++)
		{
			// RefMode 0: reference on/just inside the surface (MarchReferenceRay's choice: long orbit)
			// RefMode 1: reference 3S outside the surface, like the camera position (short orbit, relies on rebasing)
			std::printf("\n=== %s, reference %s ===\n", L.Name, RefMode == 0 ? "at surface (inside if found)" : "3S outside surface (escaping orbit)");
			std::printf("  %-7s %5s %4s | %9s %9s %9s %9s | %7s %7s %6s %7s %8s | %9s %9s %7s %8s\n", "zoom", "n", "in", "abs p50", "abs p99", "march max", "rel p50", "iter==", "di<=1", "glitch", "chaotic", "rebases", "naive abs", "naive rel", "iter==", "n.glitch");
			for (double S : Depths)
			{
				QV Ref = Surface;
				bool bInside = false;
				if (RefMode == 0)
				{
					for (double F : {0.01, 0.03, 0.1, 0.3, 1.0})
					{
						QV Q(Surface.X + (quad)D[0] * (quad)(F * S), Surface.Y + (quad)D[1] * (quad)(F * S), Surface.Z + (quad)D[2] * (quad)(F * S));
						if (!QDE(Q).bEscaped) { Ref = Q; bInside = true; break; }
					}
				}
				else
					Ref = QV(Surface.X - (quad)D[0] * (quad)(3 * S), Surface.Y - (quad)D[1] * (quad)(3 * S), Surface.Z - (quad)D[2] * (quad)(3 * S));
				FDDVec3 CRef(ToDD(Ref.X), ToDD(Ref.Y), ToDD(Ref.Z));
				FDepthStats St = RunDepth(Ctx, K, CRef, S, NSamples, 1000 + (uint64_t)(-std::log10(S)), bVerbose && S > 1e-28);
				std::printf("  %-7.0e %5d %4s | %9.1e %9.1e %9.1e %9.1e | %6.1f%% %6.1f%% %6d %7d %4.1f/%-3.0f | %9.1e %9.1e %6.1f%% %8d\n", S, St.N, bInside ? "yes" : "no",
					St.PertAbs.Pct(0.5), St.PertAbs.Pct(0.99), St.PertMarch.Max(), St.PertRel.Pct(0.5),
					100.0 * St.IterMatch / St.N, 100.0 * (St.IterMatch + St.IterOff1) / St.N, St.Glitch, St.Chaotic, St.MeanRebases, St.MaxRebases,
					St.NaiveAbs.Pct(0.5), St.NaiveRel.Pct(0.5), 100.0 * St.NaiveIterMatch / St.N, St.NaiveGlitch);
				// The +z pole location is numerically chaotic (results change between 40, 60 and 100 digits,
				// see scripts/polar_chaos.py); it is reported but not scored.
				// Pass: <= 1% marching-relevant glitches and median relative DE error <= 3e-5 (float epsilon
				// times typical orbit conditioning), from 1e-3 down to 1e-27.
				if (!L.bChaotic && S >= 1e-27 && (St.Glitch > St.N / 100 || St.PertRel.Pct(0.5) > 3e-5))
				{
					Failures++;
					std::printf("  ^^^ FAIL (glitches %d, rel p50 %.2e)\n", St.Glitch, St.PertRel.Pct(0.5));
				}
			}
		}
	}
	std::printf("\n%s\n", Failures ? "PERTURBED DE TEST: FAILURES" : "PERTURBED DE TEST: ALL DEPTHS OK");
	return Failures ? 1 : 0;
}
