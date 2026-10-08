// Times single-iteration micro-kernels (BenchLab.hlsl) on the Vulkan device.
#include "LabCommon.h"
using namespace FractalMath;
int main()
{
	FVkContext Ctx;
	struct FKernel { const char* Name; const char* Spv; const char* Entry; };
	FKernel Ks[] = {
		{"perturbed step, runtime power", "BenchPerturbed", "BenchPerturbed"},
		{"perturbed step, static power 8", "BenchPerturbed8", "BenchPerturbed"},
		{"plain trig-free step, static power 8", "BenchDirect8", "BenchDirect"},
		{"plain step, runtime power", "BenchDirect", "BenchDirect"},
		{"previous shader step (HW acos/atan2/pow/sin/cos)", "BenchOldHW", "BenchOldHW"},
	};
	const int Threads = 64 * 2048, Iters = 256;
	FVkBuffer CB = Ctx.CreateBuffer(80, true), Out = Ctx.CreateBuffer((size_t)Threads * 16);
	float* P = CB.As<float>();
	PackPowerRef(FDVec3(0.31, -0.52, 0.41), 8.0, P);
	P[12] = 1e-9f; P[13] = -2e-9f; P[14] = 3e-9f; P[15] = 0;
	P[16] = 8.0f; P[17] = 0.5f; // power, chain damping
	std::printf("device %s, %d threads x %d iterations\n", Ctx.DeviceName.c_str(), Threads, Iters);
	for (const FKernel& K : Ks)
	{
		FVkKernel Kern(Ctx, LabSpv(K.Spv), K.Entry, {true, false});
		Kern.Dispatch({&CB, &Out}, Threads / 64, 1, 1);
		double Best = 1e30;
		for (int R = 0; R < 5; R++) Best = std::min(Best, Kern.Dispatch({&CB, &Out}, Threads / 64, 1, 1));
		std::printf("  %-52s %7.2f ns/iteration\n", K.Name, 1e6 * Best / ((double)Threads * Iters));
	}
	return 0;
}
