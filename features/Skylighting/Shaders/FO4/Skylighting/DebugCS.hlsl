#include "FO4/FO4ShaderData.hlsli"
#include "FO4/GBuffer.hlsli"

#define SKYLIGHTING_PROBE_REGISTER t1
#include "Skylighting/Skylighting.hlsli"

Texture2D<float2> Normal : register(t0);
RWTexture2D<float4> Debug : register(u0);

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	uint2 dimensions;
	Debug.GetDimensions(dimensions.x, dimensions.y);
	if (any(pixel >= dimensions))
		return;

	// FO4: pixels are DR texels; shared depth takes unadjusted UVs.
	float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition((pixel + 0.5) * SharedData::BufferDim.zw);
	float depth = SharedData::GetDepth(uv);
	float visibility = 0.0;
	if (depth < 1.0) {
		float4 view = mul(FrameBuffer::CameraProjInverse, float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
		float3 positionMS = FrameBuffer::ViewToWorld(view.xyz / view.w);
#ifdef SKYLIGHTING_UP_VISIBILITY
		visibility = saturate(SphericalHarmonics::Unproject(Skylighting::SampleNoBias(positionMS), float3(0, 0, 1)));
#else
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(FO4GBuffer::DecodeViewNormal(Normal[pixel]), false));
		visibility = Skylighting::GetSkylightingDiffuse(Skylighting::Sample(positionMS, normalWS), positionMS, normalWS);
#endif
	}
	Debug[pixel] = float4(visibility.xxx, 1.0);
}
