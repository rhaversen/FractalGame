// Micro-benchmarks of a single iteration (pure ALU, no orbit loads, no divergence).
#include "FractalPowerMap.ush"
struct FBenchParams { float4 Ref0; float4 Ref1; float4 Ref2; float4 D0; float4 Misc; };
[[vk::binding(0, 0)]] cbuffer BenchCB : register(b0) { FBenchParams P; };
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> OutA : register(u0);

#ifndef BENCH_ITERS
#define BENCH_ITERS 256
#endif

// Perturbed step (path B) chained on itself
[numthreads(64, 1, 1)]
void BenchPerturbed(uint3 Id : SV_DispatchThreadID)
{
	FPPowerRef Ref = FP_DecodePowerRef(P.Ref0, P.Ref1, P.Ref2);
	float3 D = P.D0.xyz * (1.0f + 1e-3f * (float)Id.x);
	float3 DC = D;
	float Power = P.Misc.x;
	int PowerInt = FP_IntegerPower(Power);
	float Acc = 0.0f;
	[loop]
	for (int I = 0; I < BENCH_ITERS; ++I)
	{
		float3 W = Ref.Z + D;
		float RW2 = dot(W, W);
		float InvRW = rsqrt(RW2);
		float RW = RW2 * InvRW;
		float RWPowM1;
		float3 G = FP_PerturbedPowerDelta(Ref, D, W, RW, InvRW, Power, PowerInt, RWPowM1);
		D = G * P.Misc.y + DC;   // Misc.y keeps the chain bounded
		Acc += RWPowM1;
	}
	OutA[Id.x] = float4(D, Acc);
}

// Plain Mandelbulb step chained on itself
[numthreads(64, 1, 1)]
void BenchDirect(uint3 Id : SV_DispatchThreadID)
{
	float3 Z = P.Ref0.xyz * (1.0f + 1e-3f * (float)Id.x);
	float3 C = Z;
	float Power = P.Misc.x;
	int PowerInt = FP_IntegerPower(Power);
	float Acc = 0.0f;
	[loop]
	for (int I = 0; I < BENCH_ITERS; ++I)
	{
		float RW2 = dot(Z, Z);
		float InvRW = rsqrt(RW2);
		float RW = RW2 * InvRW;
		float RWPowM1;
		float3 G = FP_MandelbulbPower(Z, RW, InvRW, Power, PowerInt, RWPowM1);
		Z = normalize(G + C) * 0.9f;  // stay bounded
		Acc += RWPowM1;
	}
	OutA[Id.x] = float4(Z, Acc);
}

// The previous shader's iteration (hardware acos/atan2/pow/sin/cos), for reference
[numthreads(64, 1, 1)]
void BenchOldHW(uint3 Id : SV_DispatchThreadID)
{
	float3 Z = P.Ref0.xyz * (1.0f + 1e-3f * (float)Id.x);
	float3 C = Z;
	float Power = P.Misc.x;
	float DR = 1.0f;
	[loop]
	for (int I = 0; I < BENCH_ITERS; ++I)
	{
		float R = length(Z);
		float Theta = acos(clamp(Z.z / R, -1.0f, 1.0f));
		float Phi = atan2(Z.y, Z.x);
		DR = pow(R, Power - 1.0f) * Power * DR + 1.0f;
		float ZR = pow(R, Power);
		float ST = sin(Theta * Power);
		float3 G = ZR * float3(ST * cos(Phi * Power), ST * sin(Phi * Power), cos(Theta * Power));
		Z = normalize(G + C) * 0.9f;
	}
	OutA[Id.x] = float4(Z, DR);
}
