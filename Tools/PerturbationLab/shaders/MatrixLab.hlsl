// Verifies Unreal's matrix convention: a FMatrix44f uploaded verbatim (row-major memory) into a
// cbuffer and compiled with -Zpr must satisfy mul(v, M) == v * M (row-vector convention).
struct FMatParams { float4x4 M; float4 V; };
[[vk::binding(0, 0)]] cbuffer MatCB : register(b0) { FMatParams P; };
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> OutA : register(u0);
[numthreads(1, 1, 1)]
void MatrixMain(uint3 Id : SV_DispatchThreadID)
{
	OutA[0] = mul(P.V, P.M);
	OutA[1] = float4(P.M[0][1], P.M[1][0], P.M[3][0], P.M[0][3]); // element access: M[row][col]
}
