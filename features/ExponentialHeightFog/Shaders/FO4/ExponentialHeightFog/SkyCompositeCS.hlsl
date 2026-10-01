#define FO4_FOG_SAMPLER s0
#include "FO4/ExponentialHeightFogConsumer.hlsli"

Texture2D<float4> SceneColor : register(t0);
RWTexture2D<float4> OutputColor : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	uint width, height;
	SceneColor.GetDimensions(width, height);
	if (id.x >= width || id.y >= height)
		return;
	float4 color = SceneColor.Load(int3(id.xy, 0));
	float2 activeSize = SharedData::BufferDim.xy * FrameBuffer::DynamicResolutionParams1.xy;
	// FO4: all sky layers must be fogged once, after the native forward sky group.
	if (all(float2(id.xy) < activeSize) && SharedData::DepthTexture.Load(int3(id.xy, 0)).x >= 1.0)
		FO4Fog::ApplySky(float2(id.xy) + 0.5, color.rgb);
	OutputColor[id.xy] = color;
}
