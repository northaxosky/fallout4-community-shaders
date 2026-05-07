// SSS apply pass: multiplies the mask into kDiffuseBuffer (RT 58) with N.L gating against the sun direction.
// Runs in the post-call thunk on DrawWorld::DeferredLightsImpl. Output goes to a scratch UAV; the CPU
// follows up with a CopyResource back into kDiffuseBuffer.

Texture2D<unorm float> ShadowsTexture : register(t0);
Texture2D<float4>      NormalTexture  : register(t1);
Texture2D<float4>      DiffuseTexture : register(t2);
RWTexture2D<float4>    Output         : register(u0);

cbuffer PerFrame : register(b0)
{
	float3 SunDirectionWS;   // Normalized world-space sun direction (toward the sun is -SunDirectionWS).
	float  ApplyContrast;    // [0, 2]; 0 = no apply, 1 = full mask multiply, >1 darkens further.
	float2 ScreenSize;
	uint   SunOnly;          // bool32: 1 = gate by N.L, 0 = global multiply.
	uint   pad0;
};

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
	int2 px = (int2)DTid.xy;
	if (px.x >= (int)ScreenSize.x || px.y >= (int)ScreenSize.y)
		return;

	float4 normalSample = NormalTexture.Load(int3(px, 0));
	// Most Bethesda gbuffer normals are stored as [0,1]-encoded XYZ. If runtime probing
	// reveals a different encoding we'll branch this; for now the simple decode covers it.
	float3 N = normalize(normalSample.xyz * 2.0 - 1.0);

	float ndotl = saturate(dot(N, -SunDirectionWS));

	float mask = ShadowsTexture.Load(int3(px, 0)).x;

	// SunOnly gate keeps the multiply away from pixels primarily lit by point lights.
	float weight = SunOnly ? smoothstep(0.05, 0.30, ndotl) : 1.0;
	float blend = saturate(weight * ApplyContrast);
	float attenuation = lerp(1.0, mask, blend);

	float4 diffuse = DiffuseTexture.Load(int3(px, 0));
	Output[px] = float4(diffuse.rgb * attenuation, diffuse.a);
}
