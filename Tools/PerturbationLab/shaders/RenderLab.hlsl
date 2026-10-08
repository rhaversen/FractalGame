// Lab entry point for the full per-pixel render (FractalRender.ush), mirroring PerturbationShader.usf.
#include "FractalRender.ush"

struct FLabRenderParams
{
	float4 RayDir00;        // xyz, w = PixelRadiusPerUnitDistance
	float4 RayDirDX;        // xyz, w = Scale
	float4 RayDirDY;        // xyz, w = Power
	float4 CameraOffset;    // xyz, w = Bailout
	float4 ReferenceCenter; // xyz, w = ConvergenceFactor
	float4 Misc;            // x = MaxRayDistance, y = DirectFootprint
	int4 IParams;           // x = MaxIterations, y = MinIterations, z = MaxRaySteps, w = OrbitLength
	int4 IParams2;          // x = Width, y = Height
};

[[vk::binding(0, 0)]] cbuffer RenderCB : register(b0) { FLabRenderParams P; };
[[vk::binding(1, 0)]] StructuredBuffer<float4> ReferenceOrbit : register(t0);
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> OutColor : register(u0);
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> OutData : register(u1);

[numthreads(8, 8, 1)]
void RenderMain(uint3 Id : SV_DispatchThreadID)
{
	if (Id.x >= (uint)P.IParams2.x || Id.y >= (uint)P.IParams2.y) return;
	FPViewParams V;
	V.RayDir00 = P.RayDir00.xyz;
	V.RayDirDX = P.RayDirDX.xyz;
	V.RayDirDY = P.RayDirDY.xyz;
	V.PixelRadiusPerUnitDistance = P.RayDir00.w;
	V.CameraOffset = P.CameraOffset.xyz;
	V.Scale = P.RayDirDX.w;
	V.ReferenceCenter = P.ReferenceCenter.xyz;
	V.Power = P.RayDirDY.w;
	V.Bailout = P.CameraOffset.w;
	V.MaxIterations = P.IParams.x;
	V.MinIterations = P.IParams.y;
	V.ConvergenceFactor = P.ReferenceCenter.w;
	V.MaxRaySteps = P.IParams.z;
	V.MaxRayDistance = P.Misc.x;
	V.OrbitLength = P.IParams.w;
	V.DirectFootprint = P.Misc.y;

	float3 Dir = FP_PixelRayDirection(V, Id.xy);
	FPMarchResult R = FP_MarchRay(ReferenceOrbit, V, Dir);
	uint Index = Id.y * (uint)P.IParams2.x + Id.x;
	OutColor[Index] = float4(FP_CompositeMarch(R, float3(0.0f, 0.0f, 0.0f), V.MaxRaySteps, V.MaxIterations), (float)R.Status);
	OutData[Index] = float4(R.Distance, (float)R.Steps, (float)R.TotalIterations, (float)R.TotalSkipped);
}
