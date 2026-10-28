#include "FO4/GBuffer.hlsli"

Texture2D<float2> Normal : register(t0);
Texture2D<float4> Material : register(t1);
Texture2D<float4> Albedo : register(t2);

RWTexture2D<float2> Mask : register(u0);
RWTexture2D<float2> ViewNormal : register(u1);
RWTexture2D<float4> AlbedoOut : register(u2);

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	// FO4: native class 5 stands in for upstream's per-pixel amount and Human profile.
	const float skin = abs(Material[pixel].w * 255.0 - 5.0) < 0.25 ? 1.0 : 0.0;
	Mask[pixel] = float2(skin, skin);
	// FO4: spherical native normals become upstream's negated octahedral normal.
	ViewNormal[pixel] = GBuffer::EncodeNormal(FO4GBuffer::DecodeViewNormal(Normal[pixel]));
	// FO4: native alpha is a material scalar; upstream's is opaque alpha.
	AlbedoOut[pixel] = float4(Albedo[pixel].rgb, 1.0);
}
