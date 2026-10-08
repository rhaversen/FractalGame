// Test kernels for FractalPerturbation.ush. Each entry point is compiled separately by the lab.
#include "FractalPerturbation.ush"

struct FLabParams
{
	float4 Params0;   // x = Power, y = Scale, z = Bailout, w = ConvergenceEpsilon
	int4 IParams0;    // x = Count, y = OrbitLength, z = MaxIterations, w = MinIterations
	float4 Params1;   // xyz = reference center (float, for the naive path)
	float4 Pad;
};

[[vk::binding(0, 0)]] cbuffer LabCB : register(b0) { FLabParams P; };
[[vk::binding(1, 0)]] StructuredBuffer<float4> InA : register(t0);
[[vk::binding(2, 0)]] StructuredBuffer<float4> ReferenceOrbit : register(t1);
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> OutA : register(u0);
[[vk::binding(4, 0)]] RWStructuredBuffer<float4> OutB : register(u1);

// Elementary function test: InA[i].x = argument
[numthreads(64, 1, 1)]
void FuncsMain(uint3 Id : SV_DispatchThreadID)
{
	int I = (int)Id.x;
	if (I >= P.IParams0.x) return;
	float4 In = InA[I];
	float S, C;
	FP_SinCos(In.x, S, C);
	OutA[I] = float4(S, C, FP_Atan2(In.y, In.z), 0.0f);
	OutB[I] = float4(FP_Log1p(In.w), FP_Expm1(In.w), sin(In.x), atan2(In.y, In.z));
}

// Single perturbation step test: InA[4*i..4*i+2] = packed reference point, InA[4*i+3].xyz = D
[numthreads(64, 1, 1)]
void StepMain(uint3 Id : SV_DispatchThreadID)
{
	int I = (int)Id.x;
	if (I >= P.IParams0.x) return;
	FPOrbitPoint Ref = FP_DecodeOrbitPoint(InA[4 * I], InA[4 * I + 1], InA[4 * I + 2]);
	float3 D = InA[4 * I + 3].xyz;
	float3 W = Ref.Z + D;
	float RW = length(W);
	float RWPow;
	float3 G = FP_PerturbedPowerDelta(Ref, D, W, RW, 1.0f / RW, P.Params0.x, FP_IntegerPower(P.Params0.x), RWPow);
	OutA[I] = float4(G, RWPow);
	// naive: g(W) - g(Ref) both evaluated directly in float
	float RPowDummy;
	float3 GN = FP_MandelbulbPower(W, RW, 1.0f / RW, P.Params0.x, FP_IntegerPower(P.Params0.x), RPowDummy) - FP_MandelbulbPower(Ref.Z, length(Ref.Z), 1.0f / length(Ref.Z), P.Params0.x, FP_IntegerPower(P.Params0.x), RPowDummy);
	OutB[I] = float4(GN, 0.0f);
}

// DE test: InA[i].xyz = dc (fractal units, relative to the reference point)
[numthreads(64, 1, 1)]
void DEMain(uint3 Id : SV_DispatchThreadID)
{
	int I = (int)Id.x;
	if (I >= P.IParams0.x) return;
	float3 DC = InA[I].xyz;
	FPDEResult R = FP_PerturbedDE(ReferenceOrbit, P.IParams0.y, DC, P.Params0.y, P.Params0.x, P.IParams0.z, P.Params0.z, P.IParams0.w, P.Params0.w);
	OutA[I] = float4(R.Distance, (float)R.Iterations, R.bEscaped ? 1.0f : 0.0f, (float)R.Rebases);
	FPDEResult RN = FP_DirectDE(P.Params1.xyz + DC, P.Params0.y, P.Params0.x, P.IParams0.z, P.Params0.z);
	OutB[I] = float4(RN.Distance, (float)RN.Iterations, RN.bEscaped ? 1.0f : 0.0f, 0.0f);
}
