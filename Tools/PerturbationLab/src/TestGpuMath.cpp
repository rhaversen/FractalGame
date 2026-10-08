// GPU-side maths validation: runs FractalPerturbation.ush (compiled by DXC) on Vulkan and compares
// against __float128 ground truth.
#include "LabCommon.h"
#include <random>
#include <map>
#include <cstdlib>

using namespace FractalMath;
static int Failures = 0;
static FILE* TinyFile = nullptr;

static void Check(const char* Name, double Value, double Tol)
{
	bool Ok = Value <= Tol;
	if (!Ok) Failures++;
	std::printf("  %-58s %.3e (tol %.1e) %s\n", Name, Value, Tol, Ok ? "OK" : "FAIL");
}

static void TestFunctions(FVkContext& Ctx)
{
	std::printf("\n[1] Elementary functions (custom HLSL implementations vs quad)\n");
	const int N = 200000;
	FVkKernel K(Ctx, LabSpv("FuncsMain"), "FuncsMain", {true, false, false, false, false});
	FVkBuffer CB = Ctx.CreateBuffer(sizeof(FLabParams), true);
	FVkBuffer In = Ctx.CreateBuffer(N * 16), Orb = Ctx.CreateBuffer(16), OA = Ctx.CreateBuffer(N * 16), OB = Ctx.CreateBuffer(N * 16);
	CB.As<FLabParams>()->Count = N;
	std::mt19937_64 Rng(7);
	std::uniform_real_distribution<double> U(0, 1);
	float* A = In.As<float>();
	for (int I = 0; I < N; I++)
	{
		double Sign = U(Rng) < 0.5 ? -1 : 1;
		double X = (I < N / 2) ? Sign * std::pow(10.0, -38 + 37.5 * U(Rng)) : Sign * 40.0 * U(Rng);
		double Ratio = std::pow(10.0, -30 + 30 * U(Rng));
		double Y = (U(Rng) < 0.5 ? -1 : 1) * Ratio, XX = (U(Rng) < 0.5 ? -1 : 1) * (U(Rng) < 0.5 ? 1.0 : 1e-3);
		if (I % 3 == 0) std::swap(Y, XX);
		double L = (I % 2 == 0) ? (U(Rng) < 0.5 ? -1 : 1) * std::pow(10.0, -35 + 34.7 * U(Rng)) : -0.5 + 1.5 * U(Rng);
		A[4 * I + 0] = (float)X;
		A[4 * I + 1] = (float)Y;
		A[4 * I + 2] = (float)XX;
		A[4 * I + 3] = (float)L;
	}
	K.Dispatch({&CB, &In, &Orb, &OA, &OB}, (N + 63) / 64, 1, 1);
	float* RA = OA.As<float>();
	float* RB = OB.As<float>();
	FStats SinRelSmall, SinAbs, CosAbs, AtanRelSmall, AtanAbs, LogRel, ExpRel, HwSinRelSmall, HwAtanRelSmall;
	for (int I = 0; I < N; I++)
	{
		quad X = (quad)A[4 * I], Y = (quad)A[4 * I + 1], XX = (quad)A[4 * I + 2], L = (quad)A[4 * I + 3];
		quad S = sinq(X), C = cosq(X), At = atan2q(Y, XX);
		if (QAbs(X) < 0.5Q && X != 0)
		{
			SinRelSmall.Add((double)(QAbs((quad)RA[4 * I] - S) / QAbs(S)));
			HwSinRelSmall.Add((double)(QAbs((quad)RB[4 * I + 2] - S) / QAbs(S)));
		}
		SinAbs.Add((double)QAbs((quad)RA[4 * I] - S));
		CosAbs.Add((double)QAbs((quad)RA[4 * I + 1] - C));
		if (QAbs(At) < 0.5Q && At != 0)
		{
			AtanRelSmall.Add((double)(QAbs((quad)RA[4 * I + 2] - At) / QAbs(At)));
			HwAtanRelSmall.Add((double)(QAbs((quad)RB[4 * I + 3] - At) / QAbs(At)));
		}
		AtanAbs.Add((double)QAbs((quad)RA[4 * I + 2] - At));
		if (L != 0)
		{
			quad LP = log1pq(L), EM = expm1q(L);
			LogRel.Add((double)(QAbs((quad)RB[4 * I] - LP) / QAbs(LP)));
			ExpRel.Add((double)(QAbs((quad)RB[4 * I + 1] - EM) / QAbs(EM)));
		}
	}
	std::printf("  device: %s\n", Ctx.DeviceName.c_str());
	Check("FP_SinCos sin: max rel err, |x| in [1e-38, 0.5]", SinRelSmall.Max(), 5e-7);
	Check("FP_SinCos sin: max abs err, |x| <= 40", SinAbs.Max(), 5e-7);
	Check("FP_SinCos cos: max abs err, |x| <= 40", CosAbs.Max(), 5e-7);
	Check("FP_Atan2: max rel err, small angles down to 1e-30", AtanRelSmall.Max(), 5e-7);
	Check("FP_Atan2: max abs err, all quadrants", AtanAbs.Max(), 5e-7);
	Check("FP_Log1p: max rel err, x in [1e-35, 1]", LogRel.Max(), 5e-7);
	Check("FP_Expm1: max rel err, x in [1e-35, 1]", ExpRel.Max(), 5e-7);
	std::printf("  (info) driver sin()   max rel err for small x: %.3e  -- lavapipe is accurate; D3D allows 8e-4 abs\n", HwSinRelSmall.Max());
	std::printf("  (info) driver atan2() max rel err small angles: %.3e\n", HwAtanRelSmall.Max());
	for (FVkBuffer* B : {&CB, &In, &Orb, &OA, &OB}) Ctx.DestroyBuffer(*B);
}

struct FStepCase
{
	float Z[3];
	float D[3];
	const char* Kind;
};

static void TestStep(FVkContext& Ctx, double Power)
{
	std::printf("\n[2] Single perturbation step g(Z+d)-g(Z), power %.2f (GPU float vs quad exact)\n", Power);
	std::mt19937_64 Rng(11 + (int)(Power * 10));
	std::uniform_real_distribution<double> U(-1, 1);
	std::normal_distribution<double> G(0, 1);
	std::vector<FStepCase> Cases;
	auto RandDir = [&](double Len, double Out[3]) {
		double V[3] = {G(Rng), G(Rng), G(Rng)};
		double L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
		for (int I = 0; I < 3; I++) Out[I] = V[I] / L * Len;
	};
	const char* Kinds[] = {"generic", "near-axis", "on-axis", "near-origin", "large-d"};
	for (int Kind = 0; Kind < 5; Kind++)
	{
		for (int I = 0; I < 6000; I++)
		{
			double Z[3];
			RandDir(0.05 + 1.15 * std::fabs(U(Rng)), Z);
			if (Kind == 1) { double S = std::pow(10.0, -7 + 6 * std::fabs(U(Rng))); Z[0] *= S; Z[1] *= S; }
			if (Kind == 2) { Z[0] = 0; Z[1] = 0; }
			if (Kind == 3) { double S = std::pow(10.0, -4 + 3 * std::fabs(U(Rng))); for (double& V : Z) V *= S; }
			double ZLen = std::sqrt(Z[0] * Z[0] + Z[1] * Z[1] + Z[2] * Z[2]);
			double DLen = (Kind == 4) ? ZLen * (0.05 + 2.0 * std::fabs(U(Rng))) : ZLen * std::pow(10.0, -30 + 29 * std::fabs(U(Rng)));
			double D[3];
			RandDir(DLen, D);
			FStepCase C;
			for (int J = 0; J < 3; J++) { C.Z[J] = (float)Z[J]; C.D[J] = (float)D[J]; }
			C.Kind = Kinds[Kind];
			// The DE loop rebases before calling the step when |Z+d| < |d|; skip those (tested separately).
			double W[3] = {(double)C.Z[0] + C.D[0], (double)C.Z[1] + C.D[1], (double)C.Z[2] + C.D[2]};
			double WL = std::sqrt(W[0] * W[0] + W[1] * W[1] + W[2] * W[2]);
			double DL = std::sqrt((double)C.D[0] * C.D[0] + (double)C.D[1] * C.D[1] + (double)C.D[2] * C.D[2]);
			if (WL < DL) continue;
			Cases.push_back(C);
		}
	}
	const int N = (int)Cases.size();
	FVkKernel K(Ctx, LabSpv("StepMain"), "StepMain", {true, false, false, false, false});
	FVkBuffer CB = Ctx.CreateBuffer(sizeof(FLabParams), true);
	FVkBuffer In = Ctx.CreateBuffer((size_t)N * 64), Orb = Ctx.CreateBuffer(16), OA = Ctx.CreateBuffer((size_t)N * 16), OB = Ctx.CreateBuffer((size_t)N * 16);
	CB.As<FLabParams>()->Count = N;
	CB.As<FLabParams>()->Power = (float)Power;
	for (int I = 0; I < N; I++)
	{
		// reuse the CPU packer: input is the float position, i.e. the GPU reference Z~
		FDVec3 Z((double)Cases[I].Z[0], (double)Cases[I].Z[1], (double)Cases[I].Z[2]);
		float* Dst = In.As<float>() + (size_t)I * 16;
		PackPowerRef(Z, Power, Dst); // reference block (3 float4)
		Dst[12] = Cases[I].D[0];
		Dst[13] = Cases[I].D[1];
		Dst[14] = Cases[I].D[2];
		Dst[15] = 0;
	}
	K.Dispatch({&CB, &In, &Orb, &OA, &OB}, (N + 63) / 64, 1, 1);
	float* RA = OA.As<float>();
	float* RB = OB.As<float>();
	struct FBucket { FStats Pert, Naive; };
	std::map<std::string, int> Underflow;
	std::map<int, double> MagMax;
	double ContextMax = 0;
	std::vector<std::pair<std::string, FBucket>> Buckets;
	auto Bucket = [&](const std::string& Name) -> FBucket& {
		for (auto& B : Buckets) if (B.first == Name) return B.second;
		Buckets.push_back({Name, FBucket()});
		return Buckets.back().second;
	};
	for (int I = 0; I < N; I++)
	{
		QVec3 Ex = QExactDelta(Cases[I].Z, Cases[I].D, (quad)Power);
		quad ExLen = QLen(Ex);
		if (ExLen == 0) continue;
		// Updates below float's normal range are negligible next to dc in the real iteration and are
		// flushed to zero by GPUs anyway; they are counted, not scored.
		if (ExLen < 1e-35Q) { Underflow[Cases[I].Kind]++; continue; }
		{
			// quad itself only resolves g(Z+d)-g(Z) to ~1e-34/(|d|/|Z|): hand very small offsets to mpmath.
			double DL = std::sqrt((double)Cases[I].D[0] * Cases[I].D[0] + (double)Cases[I].D[1] * Cases[I].D[1] + (double)Cases[I].D[2] * Cases[I].D[2]);
			double ZL = std::sqrt((double)Cases[I].Z[0] * Cases[I].Z[0] + (double)Cases[I].Z[1] * Cases[I].Z[1] + (double)Cases[I].Z[2] * Cases[I].Z[2]);
			if (DL < 1e-25 * ZL)
			{
				std::fprintf(TinyFile, "%.17g,%s,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n", Power, Cases[I].Kind,
					Cases[I].Z[0], Cases[I].Z[1], Cases[I].Z[2], Cases[I].D[0], Cases[I].D[1], Cases[I].D[2], RA[4 * I], RA[4 * I + 1], RA[4 * I + 2]);
				continue;
			}
		}
		QVec3 Got{(quad)RA[4 * I], (quad)RA[4 * I + 1], (quad)RA[4 * I + 2]};
		QVec3 Nv{(quad)RB[4 * I], (quad)RB[4 * I + 1], (quad)RB[4 * I + 2]};
		// error relative to the size of the update (|g(Z+d)-g(Z)|) -- the quantity the orbit needs
		double EP = (double)(QLen(Got - Ex) / ExLen);
		double EN = (double)(QLen(Nv - Ex) / ExLen);
		double DLen = std::sqrt((double)Cases[I].D[0] * Cases[I].D[0] + (double)Cases[I].D[1] * Cases[I].D[1] + (double)Cases[I].D[2] * Cases[I].D[2]);
		double ZLen = std::sqrt((double)Cases[I].Z[0] * Cases[I].Z[0] + (double)Cases[I].Z[1] * Cases[I].Z[1] + (double)Cases[I].Z[2] * Cases[I].Z[2]);
		std::string Kind = Cases[I].Kind;
		if (Kind == "generic")
		{
			int Dec = (int)std::floor(std::log10(DLen / ZLen));
			char Buf[64];
			std::snprintf(Buf, sizeof(Buf), "generic |d|/|Z| ~ 1e%d", Dec - Dec % 5);
			Kind = Buf;
		}
		Bucket(Kind).Pert.Add(EP);
		Bucket(Kind).Naive.Add(EN);
		if (getenv("LAB_DUMP") && (EP > 3e-6 && ExLen >= 1e-25Q && Power == 8.0))
		{
			double Rho = std::sqrt((double)Cases[I].Z[0] * Cases[I].Z[0] + (double)Cases[I].Z[1] * Cases[I].Z[1]);
			std::printf("    WORST %-12s err %.2e |G| %.2e |Z| %.3e rho/|Z| %.2e |d|/|Z| %.2e  Z=(%.9g,%.9g,%.9g) D=(%.6g,%.6g,%.6g) GotRel=(%.2e,%.2e,%.2e)\n", Cases[I].Kind, EP, (double)ExLen, ZLen, Rho / ZLen, DLen / ZLen,
				Cases[I].Z[0], Cases[I].Z[1], Cases[I].Z[2], Cases[I].D[0], Cases[I].D[1], Cases[I].D[2],
				(double)((Got.X - Ex.X) / ExLen), (double)((Got.Y - Ex.Y) / ExLen), (double)((Got.Z - Ex.Z) / ExLen));
		}
		// error by magnitude of the exact update, all kinds pooled
		int Mag = (int)std::floor(std::log10((double)ExLen));
		MagMax[Mag - ((Mag % 5) + 5) % 5] = std::max(MagMax[Mag - ((Mag % 5) + 5) % 5], EP);
		// Natural scale of the update: the local Lipschitz bound p R^(p-1) |d|. For non-integer powers the
		// map jumps across its seams (-x half-plane, -z axis), where the update is O(R^p) and only
		// accuracy relative to R^p is meaningful.
		double Lip = Power * std::pow(ZLen, Power - 1.0) * DLen;
		double Natural = std::max((double)ExLen, Lip);
		if (Power != std::floor(Power) && (double)ExLen > 10.0 * Lip) Natural = std::max(Natural, std::pow(ZLen, Power));
		ContextMax = std::max(ContextMax, (double)QLen(Got - Ex) / Natural);
	}
	std::printf("  %-28s %8s %12s %12s %12s %14s\n", "case", "n", "pert p50", "pert p99.9", "pert max", "naive p50");
	for (auto& B : Buckets)
	{
		double Max = B.second.Pert.Max();
		std::printf("  %-28s %8zu %12.2e %12.2e %12.2e %14.2e\n", B.first.c_str(), B.second.Pert.N(), B.second.Pert.Pct(0.5), B.second.Pert.Pct(0.999), Max, B.second.Naive.Pct(0.5));
	}
	std::printf("  max error by |update| magnitude:");
	for (auto& M : MagMax) std::printf("  [1e%d..1e%d): %.1e", M.first, M.first + 5, M.second);
	std::printf("\n");
	Check("max error relative to natural scale max(|G|, pR^(p-1)|d|)", ContextMax, Power == std::floor(Power) ? 5e-6 : 1e-4);
	double Strict = 0;
	for (auto& M : MagMax) if (M.first >= -25) Strict = std::max(Strict, M.second);
	if (Power == std::floor(Power)) Check("max error relative to |G| for |G| >= 1e-25 (integer power)", Strict, 1e-5);
	for (auto& U : Underflow) std::printf("  (%s: %d cases with |update| < 1e-35 not scored)\n", U.first.c_str(), U.second);
	for (FVkBuffer* B : {&CB, &In, &Orb, &OA, &OB}) Ctx.DestroyBuffer(*B);
}

int main()
{
	FVkContext Ctx;
	TinyFile = std::fopen(LAB_OUT_DIR "/step_tiny_cases.csv", "w");
	TestFunctions(Ctx);
	for (double P : {8.0, 2.0, 3.0, 7.5})
		TestStep(Ctx, P);
	std::fclose(TinyFile);
	std::printf("\n(cases with |d|/|Z| < 1e-25 written to out/step_tiny_cases.csv; verify with: python3 scripts/verify_step_mpmath.py)\n");
	std::printf("\n%s\n", Failures ? "SOME GPU MATH TESTS FAILED" : "ALL GPU MATH TESTS PASSED");
	return Failures ? 1 : 0;
}
