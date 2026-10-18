// Lab entry point for the full per-pixel render (FractalRender.ush), mirroring PerturbationShader.usf.
// Compiled once per formula (-D FP_FRACTAL_TYPE=n).
#include "FractalRender.ush"

struct FLabRenderParams
{
	float4 RayDir00;        // xyz, w = PixelRadiusPerUnitDistance
	float4 RayDirDX;        // xyz, w = Scale
	float4 RayDirDY;        // xyz, w = Power
	float4 CameraOffset;    // xyz, w = Bailout
	float4 ReferenceCenter; // xyz
	float4 Misc;            // x = MaxRayDistance, y = DirectFootprint, zw = full-resolution pixels per marched pixel (PixelStep)
	int4 IParams;           // x = MaxIterations, z = MaxRaySteps, w = OrbitLength
	int4 IParams2;          // x = Width, y = Height
};

[[vk::binding(0, 0)]] cbuffer RenderCB : register(b0) { FLabRenderParams P; };
[[vk::binding(1, 0)]] StructuredBuffer<float4> ReferenceOrbit : register(t0);
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> OutColor : register(u0);
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> OutData : register(u1);
[[vk::binding(4, 0)]] RWStructuredBuffer<float4> OutShaded : register(u2);
[[vk::binding(5, 0)]] RWStructuredBuffer<float4> OutEnd : register(u3);   // x = last iterations, y = last estimate escaped

// The step-count palette the CPU ground truth image (Render.cpp ShadeCPU) uses, so the perturbation and truth columns of
// the comparison panels are directly comparable; the game itself shades with FP_Shade (the "shaded" column).
float3 LabStepPalette(FPMarchResult R, int MaxRaySteps, int MaxIterations)
{
	float StepFactor = R.Steps > 0 ? saturate(R.Steps / max((float)MaxRaySteps, 1.0f)) : 0.0f;
	float T = sqrt(StepFactor);
	float3 Color = lerp(lerp(lerp(float3(0.0f, 0.0f, 0.05f), float3(0.05f, 0.15f, 0.3f), saturate(T / 0.15f)), float3(0.2f, 0.4f, 0.8f), saturate((T - 0.15f) / 0.25f)), float3(0.6f, 0.8f, 1.0f), saturate((T - 0.4f) / 0.6f));
	if (R.Steps > 0)
	{
		float IterFactor = saturate(R.TotalIterations / max((float)(MaxIterations * max(R.Steps, 1)), 1.0f));
		if (IterFactor > 0.1f) Color = lerp(Color, float3(0.3f, 0.5f, 0.6f), ((IterFactor - 0.1f) / 0.9f) * ((IterFactor - 0.1f) / 0.9f));
		if (IterFactor > 0.9f) Color = lerp(Color, float3(0.3f, 0.9f, 0.7f), pow((IterFactor - 0.9f) / 0.1f, 5.0f));
	}
	return Color;
}

[numthreads(8, 8, 1)]
void RenderMain(uint3 Id : SV_DispatchThreadID)
{
	if (Id.x >= (uint)P.IParams2.x || Id.y >= (uint)P.IParams2.y) return;
	FPViewParams V;
	V.RayDir00 = P.RayDir00.xyz;
	V.RayDirDX = P.RayDirDX.xyz;
	V.RayDirDY = P.RayDirDY.xyz;
	V.PixelStep = P.Misc.zw;
	V.PixelRadiusPerUnitDistance = P.RayDir00.w;
	V.CameraOffset = P.CameraOffset.xyz;
	V.Scale = P.RayDirDX.w;
	V.ReferenceCenter = P.ReferenceCenter.xyz;
	V.Power = P.RayDirDY.w;
	V.Bailout = P.CameraOffset.w;
	V.MaxIterations = P.IParams.x;
	V.MaxRaySteps = P.IParams.z;
	V.MaxRayDistance = P.Misc.x;
	V.OrbitLength = P.IParams.w;
	V.DirectFootprint = P.Misc.y;

	float3 Dir = FP_PixelRayDirection(V, Id.xy);
	FPMarchResult R = FP_MarchRay(ReferenceOrbit, V, Dir);
	uint Index = Id.y * (uint)P.IParams2.x + Id.x;
	float3 StepColor = LabStepPalette(R, V.MaxRaySteps, V.MaxIterations);
	if (R.Status == FP_MISS_DISTANCE)
	{
		float StepFactor = R.Steps > 0 ? saturate(R.Steps / max((float)V.MaxRaySteps, 1.0f)) : 0.0f;
		StepColor = StepColor * pow(StepFactor, 0.1f); // over a black background
	}
	OutColor[Index] = float4(StepColor, (float)R.Status);
	OutEnd[Index] = float4((float)R.LastIterations, R.bLastEscaped ? 1.0f : 0.0f, 0.0f, 0.0f);
	OutShaded[Index] = float4(FP_Shade(ReferenceOrbit, V, R, Dir, float3(0.02f, 0.03f, 0.06f)), 1.0f);
	OutData[Index] = float4(R.Distance, (float)R.Steps, (float)R.TotalIterations, (float)R.TotalSkipped);
}
