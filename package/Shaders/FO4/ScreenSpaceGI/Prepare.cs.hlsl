#include "Common/Color.hlsli"
#include "FO4/ScreenSpaceGI/Contracts.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<float2> Normal : register(t0);
#ifdef GI_SPECULAR
Texture2D<float4> Material : register(t1);
RWTexture2D<float4> NormalGloss : register(u0);
#else
RWTexture2D<float2> NormalGloss : register(u0);
#endif
#ifdef GI
Texture2D<float4> Albedo : register(t2);
Texture2D<float3> Diffuse : register(t3);
Texture2D<float3> DiffuseTiled : register(t4);
Texture2D<float4> Emissive : register(t5);
RWTexture2D<float3> ShadedDiffuse : register(u1);
#endif

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	if (any(pixel >= uint2(FrameDim)))
		return;
	float3 normal = FO4GBuffer::DecodeViewNormal(Normal[pixel]);
	// FO4: spherical native normals become upstream's negated octahedral normal/gloss input.
#ifdef GI_SPECULAR
	NormalGloss[pixel] = float4(GBuffer::EncodeNormal(normal), Material[pixel].x, 0);
#else
	NormalGloss[pixel] = GBuffer::EncodeNormal(normal);
#endif
#ifdef GI
#	ifdef QUARTER_RES
	// FULLRES_LOAD samples only the middle 2x2 texels of each 4x4 block, or the clamped texture edge.
	uint2 phase = pixel & 3;
	if (any(((phase == 0) | (phase == 3)) & (pixel != uint2(TexDim) - 1)))
		return;
#	endif
	float3 diffuse = Diffuse[pixel];
	if (ScreenSpaceGI::Tiled)
		diffuse += DiffuseTiled[pixel];
	// FO4: encode native linear radiance only at the unchanged RadianceToLinear input boundary.
	ShadedDiffuse[pixel] = Color::IrradianceToGamma(max(0, 3.0 * Albedo[pixel].rgb * diffuse + Emissive[pixel].rgb));
#endif
}
