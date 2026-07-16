// Synthesizes upscaler masks from opaque-vs-final color.
// Default permutation writes reactive (u0) + transparency (u1) for DLSS.
// TRANSPARENCY_ONLY writes only transparency (u1) so it never clobbers FFX's FSR reactive mask.

Texture2D<float4> OpaqueColor : register(t0);  // Upscaling-owned opaque copy (pre-alpha)
Texture2D<float4> FinalColor : register(t1);   // kMainTemp (post-alpha)

RWTexture2D<float> TransparencyCompositionMask : register(u1);
#ifndef TRANSPARENCY_ONLY
RWTexture2D<float> ReactiveMask : register(u0);
#endif

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;
	float4 MaskParams;  // x=ReactiveScale, y=TransparencyScale
};

// Per-channel reversible tonemap so bright HDR particles/flashes don't saturate the diff everywhere.
float3 Tonemap(float3 c)
{
	return c / (1.0 + c);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= RenderSize))
		return;

	float3 o = Tonemap(max(OpaqueColor[dispatchID.xy].rgb, 0.0));
	float3 f = Tonemap(max(FinalColor[dispatchID.xy].rgb, 0.0));

	float3 d = abs(f - o);
	float diff = max(d.r, max(d.g, d.b));

	TransparencyCompositionMask[dispatchID.xy] = saturate(diff * MaskParams.y);
#ifndef TRANSPARENCY_ONLY
	ReactiveMask[dispatchID.xy] = saturate(diff * MaskParams.x);
#endif
}
