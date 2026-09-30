#include "Common/Color.hlsli"
#include "FO4/ScreenSpaceGI/Contracts.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<float2> Normal : register(t0);
Texture2D<float4> Material : register(t1);
Texture2D<float4> Albedo : register(t2);
Texture2D<float3> Diffuse : register(t3);
Texture2D<float3> DiffuseTiled : register(t4);
Texture2D<float4> Emissive : register(t5);
RWTexture2D<float4> NormalGloss : register(u0);
RWTexture2D<float3> ShadedDiffuse : register(u1);

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	if (any(pixel >= uint2(FrameDim)))
		return;
	float3 normal = ScreenSpaceGI::DecodeViewNormal(Normal[pixel]);
	// FO4: spherical native normals become upstream's negated octahedral normal/gloss input.
	NormalGloss[pixel] = float4(GBuffer::EncodeNormal(normal), Material[pixel].x, 0);
	float3 diffuse = Diffuse[pixel];
	if (ScreenSpaceGI::Tiled)
		diffuse += DiffuseTiled[pixel];
	// FO4: encode native linear radiance only at the unchanged RadianceToLinear input boundary.
	ShadedDiffuse[pixel] = Color::IrradianceToGamma(max(0, 3.0 * Albedo[pixel].rgb * diffuse + Emissive[pixel].rgb));
}
