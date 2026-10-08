// End-to-end precision test of the perturbed distance estimator across zoom depths, per formula.
//
// For several points on the fractal surface (found by marching in quad precision, src/scenes/*.inc) and zoom
// scales S = 1e-3 ... 1e-29, sample points c = C_ref + dc with |dc| ~ S around the surface and compare
//   GPU perturbed DE   (FractalDE.ush, float deltas, double-double reference orbit)
//   GPU naive DE       (plain float iteration at float(C_ref) + dc)
// against a __float128 direct iteration at the exact point.
// Usage: test_perturbed_de [formula|all] [samples]
#include "LabCommon.h"
#include "FormulaScenes.h"
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

static FFormulaParams GParams;   // current formula's parameters (MaxIterations 200, Bailout 10)
static int Failures = 0;

template <typename F>
static FDistanceEstimate QDE(const QV& C) { return DistanceEstimate<F, quad>(C, GParams); }

/**
 * March from Start along Dir (unit) in quad until DE < Tol; returns the last (outside) point. If a step
 * lands inside the set (the DE is only an estimate), bisect between the last outside and the inside point.
 */
template <typename F>
static bool QMarchToSurface(const QV& Start, const double Dir[3], quad Tol, QV& OutOutside)
{
	auto At = [&](quad T) { return QV(Start.X + (quad)Dir[0] * T, Start.Y + (quad)Dir[1] * T, Start.Z + (quad)Dir[2] * T); };
	quad T = 0;
	for (int Step = 0; Step < 20000; Step++)
	{
		FDistanceEstimate D = QDE<F>(At(T));
		if (!D.bEscaped)
		{
			if (Step == 0) return false;
			quad Lo = T - (quad)0, Hi = T;
			Lo = T; // find an outside point behind
			quad Back = Tol;
			while (!QDE<F>(At(Hi - Back)).bEscaped && Back < 1) Back *= 2;
			Lo = Hi - Back;
			for (int K = 0; K < 200 && Hi - Lo > Tol * 0.1Q; K++)
			{
				quad Mid = 0.5Q * (Lo + Hi);
				FDistanceEstimate DM = QDE<F>(At(Mid));
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

template <typename F>
static FDepthStats RunDepth(FVkContext& Ctx, FVkKernel& K, const FDDVec3& CRef, double S, int NSamples, uint64_t Seed, bool bVerbose)
{
	FDepthStats St;
	// reference orbit (production path)
	constexpr int Stride = FormulaOrbitStride<F>();
	std::vector<float> Orbit((size_t)(GParams.MaxIterations + 1) * Stride * 4);
	const double Tol = getenv("LAB_SERIES_TOL") ? std::atof(getenv("LAB_SERIES_TOL")) : DefaultSeriesTolerance;
	const int OrbitLen = GenerateReferenceOrbit<F, FDD>(CRef, GParams, Orbit.data(), nullptr, Tol);

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
	FVkBuffer In = Ctx.CreateBuffer(DC.size() * 4), Orb = Ctx.CreateBuffer(Orbit.size() * sizeof(float));
	FVkBuffer OA = Ctx.CreateBuffer((size_t)NSamples * 16), OB = Ctx.CreateBuffer((size_t)NSamples * 16);
	FLabParams* P = CB.As<FLabParams>();
	P->Power = (float)GParams.Power; P->Scale = (float)S; P->Bailout = (float)GParams.Bailout;
	P->Count = NSamples; P->OrbitLength = OrbitLen; P->MaxIterations = GParams.MaxIterations;
	P->RefX = CRef.X.ToFloat(); P->RefY = CRef.Y.ToFloat(); P->RefZ = CRef.Z.ToFloat();
	std::memcpy(In.Mapped, DC.data(), DC.size() * 4);
	std::memcpy(Orb.Mapped, Orbit.data(), Orbit.size() * sizeof(float));
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
			Truth[I] = QDE<F>(C);
			// double-double direct iteration as a conditioning probe: if even dd and quad disagree, the
			// point is numerically chaotic (no finite precision gives a reproducible answer).
			FDDVec3 CD(CRef.X + (double)DC[4 * I], CRef.Y + (double)DC[4 * I + 1], CRef.Z + (double)DC[4 * I + 2]);
			DDBase[I] = DistanceEstimate<F, FDD>(CD, GParams);
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
		bool bConditioned = std::fabs(DDWorld - TrueWorld) < std::max(1e-5, 1e-4 * TrueWorld) && DDBase[I].bEscaped == T.bEscaped;
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
			FDistanceEstimate DD = DistanceEstimate<F, double>(CD, GParams);
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

template <typename F>
static void RunFormula(FVkContext& Ctx, const FFormulaLab& Lab, int NSamples)
{
	const int Index = static_cast<int>(F::Type);
	FVkKernel K(Ctx, LabSpv("DEMain_T" + std::to_string(Index)), "DEMain", {true, false, false, false, false});
	GParams = FFormulaParams();
	GParams.Power = getenv("LAB_POWER") ? std::atof(getenv("LAB_POWER")) : Lab.Power;
	GParams.MaxIterations = 200;
	GParams.Bailout = 10.0;
	std::printf("\n##### %s (power/scale %g) #####\n", GetFractalName(F::Type), GParams.Power);
	const double Depths[] = {1e-3, 1e-5, 1e-7, 1e-9, 1e-11, 1e-13, 1e-15, 1e-17, 1e-19, 1e-21, 1e-23, 1e-25, 1e-27, 1e-29};

	const bool bVerbose = getenv("LAB_VERBOSE") != nullptr;
	const char* OnlyDepth = getenv("LAB_DEPTH");
	for (const FTestLocation& L : Lab.Locations)
	{
		double D[3] = {L.Dir[0], L.Dir[1], L.Dir[2]};
		double Len = std::sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
		for (double& V : D) V /= Len;
		QV Start((quad)L.Start[0], (quad)L.Start[1], (quad)L.Start[2]);
		QV Surface;
		// (quad resolves positions of magnitude ~3 to ~5e-34, so the surface is located to 3e-33)
		if (!QMarchToSurface<F>(Start, D, 3e-33Q, Surface))
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
				if (OnlyDepth && std::fabs(std::log10(S) - std::atof(OnlyDepth)) > 0.5) continue;
				QV Ref = Surface;
				bool bInside = false;
				if (RefMode == 0)
				{
					for (double Fr : {0.01, 0.03, 0.1, 0.3, 1.0})
					{
						QV Q(Surface.X + (quad)D[0] * (quad)(Fr * S), Surface.Y + (quad)D[1] * (quad)(Fr * S), Surface.Z + (quad)D[2] * (quad)(Fr * S));
						if (!QDE<F>(Q).bEscaped) { Ref = Q; bInside = true; break; }
					}
				}
				else
					Ref = QV(Surface.X - (quad)D[0] * (quad)(3 * S), Surface.Y - (quad)D[1] * (quad)(3 * S), Surface.Z - (quad)D[2] * (quad)(3 * S));
				FDDVec3 CRef(ToDD(Ref.X), ToDD(Ref.Y), ToDD(Ref.Z));
				FDepthStats St = RunDepth<F>(Ctx, K, CRef, S, NSamples, 1000 + (uint64_t)(-std::log10(S)), bVerbose && S > 1e-28);
				std::printf("  %-7.0e %5d %4s | %9.1e %9.1e %9.1e %9.1e | %6.1f%% %6.1f%% %6d %7d %4.1f/%-3.0f | %9.1e %9.1e %6.1f%% %8d\n", S, St.N, bInside ? "yes" : "no",
					St.PertAbs.Pct(0.5), St.PertAbs.Pct(0.99), St.PertMarch.Max(), St.PertRel.Pct(0.5),
					100.0 * St.IterMatch / St.N, 100.0 * (St.IterMatch + St.IterOff1) / St.N, St.Glitch, St.Chaotic, St.MeanRebases, St.MaxRebases,
					St.NaiveAbs.Pct(0.5), St.NaiveRel.Pct(0.5), 100.0 * St.NaiveIterMatch / St.N, St.NaiveGlitch);
				// Pass: <= 1% marching-relevant glitches and a median relative DE error within the formula's
				// threshold (float epsilon times typical orbit conditioning), from 1e-3 down to 1e-27.
				// Numerically chaotic locations are reported but not scored.
				if (!L.bChaotic && S >= 1e-27 && (St.Glitch > St.N / 100 || St.PertRel.Pct(0.5) > Lab.MaxRelErrorP50))
				{
					Failures++;
					std::printf("  ^^^ FAIL (glitches %d, rel p50 %.2e)\n", St.Glitch, St.PertRel.Pct(0.5));
				}
			}
		}
	}
}

int main(int Argc, char** Argv)
{
	FVkContext Ctx;
	std::printf("device: %s\n", Ctx.DeviceName.c_str());
	const int NSamples = (Argc > 2) ? std::atoi(Argv[2]) : 1500;
	std::vector<EFractalFormula> Formulas;
	if (Argc > 1 && std::string(Argv[1]) != "all")
	{
		EFractalFormula F;
		if (!ParseFormula(Argv[1], F)) { std::printf("unknown formula %s\n", Argv[1]); return 2; }
		Formulas.push_back(F);
	}
	else
	{
		for (int I = 0; I < FractalFormulaCount; I++) Formulas.push_back(static_cast<EFractalFormula>(I));
	}
	for (EFractalFormula F : Formulas)
	{
		const FFormulaLab Lab = GetFormulaLab(F);
		if (Lab.Locations.empty())
		{
			std::printf("\n##### %s: no test locations, skipped #####\n", GetFractalName(F));
			continue;
		}
		VisitFormula(F, [&](auto Formula) { RunFormula<decltype(Formula)>(Ctx, Lab, NSamples); });
	}
	std::printf("\n%s\n", Failures ? "PERTURBED DE TEST: FAILURES" : "PERTURBED DE TEST: ALL DEPTHS OK");
	return Failures ? 1 : 0;
}
