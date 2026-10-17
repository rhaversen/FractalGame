// Renders views of each formula at increasing zoom (scenes in src/scenes/*.inc) with
//   (a) the GPU perturbation shader (FractalRender.ush via DXC/Vulkan),
//   (b) the same shader forced to plain float at absolute coordinates (what the old renderer did),
//   (c) a CPU double-double ground truth running the identical march,
// using the production CPU path (MarchReferenceRay + GenerateReferenceOrbit + BuildRayBasis) for (a).
// Writes PPM images to out/images/<formula>/ and prints accuracy and timing statistics.
// Usage: render_lab [width height truth(0/1) formula|all scene]
#include "LabCommon.h"
#include "FormulaScenes.h"
#include "UECameraReplica.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/stat.h>
#include <cstdlib>

using namespace FractalMath;
namespace FractalMath
{
inline FDVec3 ToDoubleVec(const TFractalVec3<quad>& A) { return FDVec3((double)A.X, (double)A.Y, (double)A.Z); }
}
using QV = TFractalVec3<quad>;

struct FRenderSettings
{
	FFormulaParams Params;        // MaxIterations 150, Bailout 10, Power from the formula's lab data
	int MaxRaySteps = 200;
	double MaxRayDistance = 60.0; // world units
};

struct FLabRenderParams
{
	float RayDir00[4];
	float RayDirDX[4];
	float RayDirDY[4];
	float CameraOffset[4];
	float ReferenceCenter[4];
	float Misc[4];
	int IParams[4];
	int IParams2[4];
};

struct FPixel { float R, G, B; int Status; float Dist; int Steps; int TotalIter; int Skipped = 0; };

static void ShadeCPU(int Steps, int TotalIter, int Status, const FRenderSettings& RS, float Out[3])
{
	auto Sat = [](double X) { return X < 0 ? 0.0 : (X > 1 ? 1.0 : X); };
	auto Lerp = [](const double A[3], const double B[3], double T, double O[3]) { for (int I = 0; I < 3; I++) O[I] = A[I] + (B[I] - A[I]) * T; };
	double SF = Steps > 0 ? Sat(Steps / (double)RS.MaxRaySteps) : 0, T = std::sqrt(SF);
	double C0[3] = {0, 0, 0.05}, C1[3] = {0.05, 0.15, 0.3}, C2[3] = {0.2, 0.4, 0.8}, C3[3] = {0.6, 0.8, 1.0}, A[3], B[3], C[3];
	Lerp(C0, C1, Sat(T / 0.15), A);
	Lerp(A, C2, Sat((T - 0.15) / 0.25), B);
	Lerp(B, C3, Sat((T - 0.4) / 0.6), C);
	if (Steps > 0)
	{
		double IF = Sat(TotalIter / std::max((double)RS.Params.MaxIterations * std::max(Steps, 1), 1.0));
		double G1[3] = {0.3, 0.5, 0.6}, G2[3] = {0.3, 0.9, 0.7}, D[3];
		if (IF > 0.1) { double G = (IF - 0.1) / 0.9; Lerp(C, G1, G * G, D); for (int I = 0; I < 3; I++) C[I] = D[I]; }
		if (IF > 0.9) { double G = (IF - 0.9) / 0.1; Lerp(C, G2, std::pow(G, 5.0), D); for (int I = 0; I < 3; I++) C[I] = D[I]; }
	}
	if (Status == 2) { double F = std::pow(SF, 0.1); for (int I = 0; I < 3; I++) C[I] = C[I] * F; }
	for (int I = 0; I < 3; I++) Out[I] = (float)C[I];
}

static void WritePPM(const std::string& Path, const std::vector<FPixel>& Img, int W, int H)
{
	FILE* F = std::fopen(Path.c_str(), "wb");
	std::fprintf(F, "P6\n%d %d\n255\n", W, H);
	for (const FPixel& P : Img)
	{
		auto Enc = [](float V) { V = V < 0 ? 0 : (V > 1 ? 1 : V); return (unsigned char)(std::pow(V, 1.0f / 2.2f) * 255.0f + 0.5f); };
		unsigned char C[3] = {Enc(P.R), Enc(P.G), Enc(P.B)};
		std::fwrite(C, 1, 3, F);
	}
	std::fclose(F);
}


template <typename F>
static bool QMarchToSurface(const QV& Start, const double Dir[3], quad Tol, QV& Out, const FRenderSettings& RS)
{
	auto At = [&](quad T) { return QV(Start.X + (quad)Dir[0] * T, Start.Y + (quad)Dir[1] * T, Start.Z + (quad)Dir[2] * T); };
	quad T = 0;
	for (int Step = 0; Step < 20000; Step++)
	{
		FDistanceEstimate D = DistanceEstimate<F, quad>(At(T), RS.Params);
		if (!D.bEscaped)
		{
			quad Hi = T, Lo = T - 1e-3Q;
			for (int K = 0; K < 300 && Hi - Lo > Tol; K++)
			{
				quad Mid = 0.5Q * (Lo + Hi);
				if (DistanceEstimate<F, quad>(At(Mid), RS.Params).bEscaped) Lo = Mid; else Hi = Mid;
			}
			Out = At(Lo);
			return true;
		}
		if ((quad)D.Distance < Tol) { Out = At(T); return true; }
		T += (quad)D.Distance * 0.9Q;
	}
	return false;
}

template <typename F>
static void RenderFormula(FVkContext& Ctx, const FFormulaLab& Lab, int W, int H, bool bTruth, double DirectFootprint, const char* SceneFilter)
{
	FRenderSettings RS;
	RS.Params.Power = getenv("LAB_POWER") ? std::atof(getenv("LAB_POWER")) : Lab.Power;
	const int Index = static_cast<int>(F::Type);
	FVkKernel KPert(Ctx, LabSpv("RenderMain_T" + std::to_string(Index)), "RenderMain", {true, false, false, false});
	FVkKernel KNaive(Ctx, LabSpv("RenderNaive_T" + std::to_string(Index)), "RenderMain", {true, false, false, false});
	const std::string ImageDir = std::string(LAB_OUT_DIR) + "/images/" + GetFractalName(F::Type);
	mkdir(ImageDir.c_str(), 0755);
	std::printf("\n##### %s (power/scale %g) #####\n", GetFractalName(F::Type), RS.Params.Power);
	std::vector<FRenderScene> Scenes;
	for (const FRenderScene& Sc : Lab.Scenes) if (!SceneFilter || std::string(Sc.Name) == SceneFilter) Scenes.push_back(Sc);

	std::printf("%-7s | %8s %8s %6s | %9s %9s %9s | %8s %8s | %8s %8s %7s\n", "scene", "ref t", "inside", "orbit", "pert ms", "naive ms", "cpu ms", "status==", "depth ok", "n.status", "n.depth", "rebuild");
	for (const FRenderScene& Sc : Scenes)
	{
		// --- locate the surface and place the camera (fractal space, high precision) ---
		double D[3] = {Sc.Dir[0], Sc.Dir[1], Sc.Dir[2]};
		double L = std::sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
		for (double& V : D) V /= L;
		QV Surface;
		if (!QMarchToSurface<F>(QV((quad)Sc.Start[0], (quad)Sc.Start[1], (quad)Sc.Start[2]), D, (quad)(Sc.Zoom * 1e-4), Surface, RS))
		{
			std::printf("%s: surface not found\n", Sc.Name);
			continue;
		}
		const double S = Sc.Zoom;
		QV CamQ(Surface.X - (quad)D[0] * (quad)(Sc.CamDistance * S), Surface.Y - (quad)D[1] * (quad)(Sc.CamDistance * S), Surface.Z - (quad)D[2] * (quad)(Sc.CamDistance * S));
		FDDVec3 Cam(ToDD(CamQ.X), ToDD(CamQ.Y), ToDD(CamQ.Z));

		// camera rotation: look at the surface point, then offset (exercise yaw/pitch/roll)
		double Pitch = std::asin(D[2]) * 180.0 / M_PI + Sc.PitchOffset;
		double Yaw = std::atan2(D[1], D[0]) * 180.0 / M_PI + Sc.YawOffset;
		double Rot[3][3], V2W[3][3], Proj[4][4], InvProj[4][4];
		UERotationMatrix(Pitch, Yaw, Sc.Roll, Rot);
		UEViewToWorldFromRotation(Rot, V2W);
		UEReversedZPerspective(45.0 * M_PI / 180.0, W, H, 10.0, Proj); // 90 deg horizontal FOV (UE default)
		InvertMatrix4(Proj, InvProj);
		FFractalRayBasis Basis = BuildRayBasis(InvProj, V2W, W, H);

		// --- production CPU path: reference ray march + reference orbit ---
		auto TRef0 = std::chrono::steady_clock::now();
		FReferenceMarchSettings MS;
		MS.Params = RS.Params;
		MS.MaxSteps = RS.MaxRaySteps; MS.MaxDistance = RS.MaxRayDistance; MS.PixelRadiusPerUnitDistance = Basis.PixelRadiusPerUnitDistance;
		double Fwd[3] = {Rot[0][0], Rot[0][1], Rot[0][2]};
		// (the plugin marches in double while Scale > 1e-9; dd is always exact)
		FReferenceMarchResult RM = MarchReferenceRay<F, FDD>(Cam, Fwd, S, MS);
		FDDVec3 CRef = OffsetDD(Cam, Fwd, RM.ReferenceDistance * S);
		std::vector<float> Orbit((size_t)(RS.Params.MaxIterations + 1) * FormulaOrbitStride<F>() * 4);
		const double SeriesTol = getenv("LAB_SERIES_TOL") ? std::atof(getenv("LAB_SERIES_TOL")) : DefaultSeriesTolerance;
		int OrbitLen = GenerateReferenceOrbit<F, FDD>(CRef, RS.Params, Orbit.data(), nullptr, SeriesTol);
		double RefMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - TRef0).count();
		FDDVec3 Off = Cam - CRef;

		// --- GPU renders ---
		FVkBuffer CB = Ctx.CreateBuffer(sizeof(FLabRenderParams), true);
		FVkBuffer Orb = Ctx.CreateBuffer(Orbit.size() * sizeof(float));
		FVkBuffer OC = Ctx.CreateBuffer((size_t)W * H * 16), OD = Ctx.CreateBuffer((size_t)W * H * 16);
		std::memcpy(Orb.Mapped, Orbit.data(), Orbit.size() * sizeof(float));
		FLabRenderParams* P = CB.As<FLabRenderParams>();
		for (int I = 0; I < 3; I++)
		{
			P->RayDir00[I] = (float)Basis.DirPixel00[I];
			P->RayDirDX[I] = (float)Basis.DirDX[I];
			P->RayDirDY[I] = (float)Basis.DirDY[I];
		}
		P->CameraOffset[0] = (float)(Off.X.ToDouble() / S);
		P->CameraOffset[1] = (float)(Off.Y.ToDouble() / S);
		P->CameraOffset[2] = (float)(Off.Z.ToDouble() / S);
		P->ReferenceCenter[0] = CRef.X.ToFloat(); P->ReferenceCenter[1] = CRef.Y.ToFloat(); P->ReferenceCenter[2] = CRef.Z.ToFloat();
		P->RayDir00[3] = (float)Basis.PixelRadiusPerUnitDistance;
		P->RayDirDX[3] = (float)S;
		P->RayDirDY[3] = (float)RS.Params.Power;
		P->CameraOffset[3] = (float)RS.Params.Bailout;
		P->ReferenceCenter[3] = 0.0f;
		P->Misc[0] = (float)RS.MaxRayDistance;
		P->Misc[1] = (float)DirectFootprint;
		P->IParams[0] = RS.Params.MaxIterations; P->IParams[1] = 0; P->IParams[2] = RS.MaxRaySteps; P->IParams[3] = OrbitLen;
		P->IParams2[0] = W; P->IParams2[1] = H;

		auto Grab = [&](std::vector<FPixel>& Img) {
			Img.resize((size_t)W * H);
			float* C = OC.As<float>();
			float* Dd = OD.As<float>();
			for (int I = 0; I < W * H; I++)
				Img[I] = {C[4 * I], C[4 * I + 1], C[4 * I + 2], (int)C[4 * I + 3], Dd[4 * I], (int)Dd[4 * I + 1], (int)Dd[4 * I + 2], (int)Dd[4 * I + 3]};
		};
		std::vector<FPixel> ImgP, ImgN, ImgT;
		auto SumIter = [&](const std::vector<FPixel>& Img) { double T = 0; for (const FPixel& X : Img) T += X.TotalIter; return T; };
		auto SumSteps = [&](const std::vector<FPixel>& Img) { double T = 0; for (const FPixel& X : Img) T += X.Steps; return T; };
		auto SumSkipped = [&](const std::vector<FPixel>& Img) { double T = 0; for (const FPixel& X : Img) T += X.Skipped; return T; };
		KPert.Dispatch({&CB, &Orb, &OC, &OD}, (W + 7) / 8, (H + 7) / 8, 1); // warm-up (JIT)
		double PertMs = KPert.Dispatch({&CB, &Orb, &OC, &OD}, (W + 7) / 8, (H + 7) / 8, 1);
		Grab(ImgP);
		KNaive.Dispatch({&CB, &Orb, &OC, &OD}, (W + 7) / 8, (H + 7) / 8, 1);
		double NaiveMs = KNaive.Dispatch({&CB, &Orb, &OC, &OD}, (W + 7) / 8, (H + 7) / 8, 1);
		Grab(ImgN);
		for (FVkBuffer* B : {&CB, &Orb, &OC, &OD}) Ctx.DestroyBuffer(*B);

		// --- CPU ground truth (double-double positions, identical march logic) ---
		double CpuMs = 0;
		if (bTruth)
		{
			ImgT.resize((size_t)W * H);
			std::atomic<int> Next(0);
			auto T0 = std::chrono::steady_clock::now();
			auto Worker = [&]() {
				for (int I = Next++; I < W * H; I = Next++)
				{
					int PX = I % W, PY = I / W;
					double Dir[3];
					RayBasisDirection(Basis, PX + 0.5, PY + 0.5, Dir);
					double DL = std::sqrt(Dir[0] * Dir[0] + Dir[1] * Dir[1] + Dir[2] * Dir[2]);
					for (double& V : Dir) V /= DL;
					double T = 0;
					int Steps = 0, Total = 0, Status = 3;
					while (Steps < RS.MaxRaySteps)
					{
						if (T >= RS.MaxRayDistance) { Status = 2; break; }
						Steps++;
						FDistanceEstimate DE = DistanceEstimate<F, FDD>(OffsetDD(Cam, Dir, T * S), RS.Params);
						Total += DE.Iterations;
						double PixelRadius = T * Basis.PixelRadiusPerUnitDistance;
						double DEW = DE.Distance / S;
						if (DEW <= PixelRadius) { Status = 1; break; }
						T += std::max(DEW, PixelRadius * 0.5);
					}
					float C[3];
					ShadeCPU(Steps, Total, Status, RS, C);
					ImgT[I] = {C[0], C[1], C[2], Status, (float)T, Steps, Total};
				}
			};
			std::vector<std::thread> Th;
			for (int K = 0; K < 4; K++) Th.emplace_back(Worker);
			for (auto& X : Th) X.join();
			CpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
		}

		// --- compare ---
		auto Compare = [&](const std::vector<FPixel>& A, int& StatusSame, int& DepthOk) {
			StatusSame = DepthOk = 0;
			for (int I = 0; I < W * H; I++)
			{
				if (A[I].Status == ImgT[I].Status) StatusSame++;
				// hit depth agrees within 2 pixel footprints, or both are misses
				double Tol = 2.0 * 2.0 * Basis.PixelRadiusPerUnitDistance * std::max((double)ImgT[I].Dist, 1e-3);
				bool BothHit = A[I].Status == 1 && ImgT[I].Status == 1;
				if ((BothHit && std::fabs(A[I].Dist - ImgT[I].Dist) <= Tol) || (A[I].Status != 1 && ImgT[I].Status != 1)) DepthOk++;
			}
		};
		int SP = 0, DP = 0, SN = 0, DN = 0;
		if (bTruth) { Compare(ImgP, SP, DP); Compare(ImgN, SN, DN); }
		std::string Base = ImageDir + "/" + Sc.Name;
		WritePPM(Base + "_perturbation.ppm", ImgP, W, H);
		WritePPM(Base + "_naive.ppm", ImgN, W, H);
		if (bTruth) WritePPM(Base + "_truth.ppm", ImgT, W, H);
		double N = (double)W * H;
		std::printf("%-7s | %8.3f %8s %6d | %9.1f %9.1f %9.1f | %7.2f%% %7.2f%% | %7.2f%% %7.2f%% %6.1fms | %6.1f %6.1f ns/it  %6.1f it/px\n", Sc.Name, RM.ReferenceDistance, RM.bReferenceInside ? "yes" : "no", OrbitLen,
			PertMs, NaiveMs, CpuMs, 100 * SP / N, 100 * DP / N, 100 * SN / N, 100 * DN / N, RefMs,
			1e6 * PertMs / std::max(SumIter(ImgP) - SumSkipped(ImgP), 1.0), 1e6 * NaiveMs / SumIter(ImgN), SumIter(ImgP) / N);
		if (getenv("LAB_STEPS")) std::printf("          StepsPerPx %.1f  ItersPerDE %.1f  executed per DE %.1f (series skip saved %.0f%%)\n", SumSteps(ImgP) / N, SumIter(ImgP) / SumSteps(ImgP),
			(SumIter(ImgP) - SumSkipped(ImgP)) / SumSteps(ImgP), 100.0 * SumSkipped(ImgP) / std::max(SumIter(ImgP), 1.0));
	}
}

int main(int Argc, char** Argv)
{
	const int W = (Argc > 1) ? std::atoi(Argv[1]) : 192;
	const int H = (Argc > 2) ? std::atoi(Argv[2]) : 120;
	const bool bTruth = (Argc > 3) ? std::atoi(Argv[3]) != 0 : true;
	const char* SceneFilter = (Argc > 5) ? Argv[5] : nullptr;
	// Hybrid switch: plain float DE once a sample's pixel footprint exceeds this many fractal units.
	const double DirectFootprint = getenv("LAB_DIRECT_FOOTPRINT") ? std::atof(getenv("LAB_DIRECT_FOOTPRINT")) : 1e-4;
	mkdir(LAB_OUT_DIR "/images", 0755);
	FVkContext Ctx;
	std::printf("device: %s, %dx%d, ground truth %s, hybrid direct footprint %.1e\n", Ctx.DeviceName.c_str(), W, H, bTruth ? "on" : "off", DirectFootprint);
	std::vector<EFractalFormula> Formulas;
	if (Argc > 4 && std::string(Argv[4]) != "all")
	{
		EFractalFormula F;
		if (!ParseFormula(Argv[4], F)) { std::printf("unknown formula %s\n", Argv[4]); return 2; }
		Formulas.push_back(F);
	}
	else
	{
		for (int I = 0; I < FractalFormulaCount; I++) Formulas.push_back(static_cast<EFractalFormula>(I));
	}
	for (EFractalFormula F : Formulas)
	{
		const FFormulaLab Lab = GetFormulaLab(F);
		if (Lab.Scenes.empty()) continue;
		VisitFormula(F, [&](auto Formula) { RenderFormula<decltype(Formula)>(Ctx, Lab, W, H, bTruth, DirectFootprint, SceneFilter); });
	}
	return 0;
}
