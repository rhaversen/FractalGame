// Evaluates the hybrid distance estimator (FP_DistanceEstimate, as the ray march calls it) at a list of samples
// dc = Scale * (CameraOffset + t * Dir) given by the CPU. Compiled per formula (-D FP_FRACTAL_TYPE=n).
#include "FractalRender.ush"

struct FProfileParams
{
	float4 A;       // x = Scale, y = Power, z = Bailout, w = DirectFootprint
	int4 I;         // x = MaxIterations, y = OrbitLength, z = Count
	float4 RefCenter;
};

[[vk::binding(0, 0)]] cbuffer ProfileCB : register(b0) { FProfileParams P; };
[[vk::binding(1, 0)]] StructuredBuffer<float4> ReferenceOrbit : register(t0);
[[vk::binding(2, 0)]] StructuredBuffer<float4> InSamples : register(t1);   // xyz = dc, w = pixel radius (world units)
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> OutDE : register(u0);     // x = DE, y = iterations, z = escaped, w = skipped

[numthreads(64, 1, 1)]
void ProfileMain(uint3 Id : SV_DispatchThreadID)
{
	int I = (int)Id.x;
	if (I >= P.I.z) return;
	FPViewParams V;
	V.HazeBase = 0;
	V.RayDir00 = 0; V.RayDirDX = 0; V.RayDirDY = 0;
	V.PixelRadiusPerUnitDistance = 0;
	V.CameraOffset = 0;
	V.Scale = P.A.x;
	V.ReferenceCenter = P.RefCenter.xyz;
	V.Power = P.A.y;
	V.Bailout = P.A.z;
	V.MaxIterations = P.I.x;
	V.MaxRaySteps = 150;
	V.MaxRayDistance = 1e30f;
	V.OrbitLength = P.I.y;
	V.DirectFootprint = P.A.w;
	float4 S = InSamples[I];
	FPFormulaArgs Args = FP_MakeFormulaArgs(V.Power, V.Scale);
	FPDEResult R = FP_DistanceEstimate(ReferenceOrbit, V, Args, S.xyz, S.w);
	OutDE[I] = float4(R.Distance, (float)R.Iterations, R.bEscaped ? 1.0f : 0.0f, (float)R.Skipped);
}
