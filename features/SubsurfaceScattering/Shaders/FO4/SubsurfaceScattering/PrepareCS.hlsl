#include "FO4/Depth.hlsli"
#include "FO4/GBuffer.hlsli"

Texture2D<float2> Normal : register(t0);
Texture2D<float4> Material : register(t1);
Texture2D<float4> Albedo : register(t2);
Texture2D<float> SceneDepth : register(t3);
Texture2D<float> Profile : register(t4);

RWTexture2D<float2> Mask : register(u0);
RWTexture2D<float2> ViewNormal : register(u1);
RWTexture2D<float4> AlbedoOut : register(u2);

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	// FO4: class 5 outside first person is skin.
	const bool skinClass = abs(Material[pixel].w * 255.0 - 5.0) < 0.25;
	const float skin = skinClass && !FO4Depth::IsFirstPerson(SceneDepth[pixel]) ? 1.0 : 0.0;
	// FO4: skin alpha is a cutout, so amount is 1; the prepass bakes the profile.
	Mask[pixel] = float2(skin, skin * Profile[pixel]);
	// FO4: spherical native normals become upstream's negated octahedral one.
	ViewNormal[pixel] = GBuffer::EncodeNormal(FO4GBuffer::DecodeViewNormal(Normal[pixel]));
	// FO4: native alpha is a material scalar; upstream's is opaque alpha.
	AlbedoOut[pixel] = float4(Albedo[pixel].rgb, 1.0);
}
