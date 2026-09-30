#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "FO4/Depth.hlsli"
#include "FO4/DynamicCubemaps/CubemapCommon.hlsli"

Texture2D<float> DepthTexture : register(t0);
Texture2D<float4> ColorTexture : register(t1);
Texture2D<float4> AlbedoTexture : register(t2);
Texture2D<float4> DiffuseTexture : register(t3);
Texture2D<float4> TiledDiffuseTexture : register(t4);
Texture2D<float4> EmissiveTexture : register(t5);
SamplerState LinearSampler : register(s0);
RWTexture2DArray<float4> CapturePosition : register(u0);
RWTexture2DArray<float4> CaptureColor : register(u1);
RWTexture2DArray<float2> CaptureUV : register(u2);

cbuffer PrepareData : register(b0)
{
	row_major float4x4 InvProj;
}

[numthreads(8, 8, 1)] void main(uint3 texel : SV_DispatchThreadID) {
	CapturePosition[texel] = 0;
	CaptureColor[texel] = 0;
	CaptureUV[texel] = 0;
	uint width, height, faces;
	CapturePosition.GetDimensions(width, height, faces);
	float2 st = (texel.xy + 0.5) / float2(width, height);
	float2 faceUV = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;
	// FO4 views down +Z; the host projection preserves upstream's visible cube directions.
	float3 viewDirection = FrameBuffer::WorldToView(DynamicCubemaps::CubeFaceDirection(texel.z, faceUV), false);
	float2 uv = FrameBuffer::ViewToUV(viewDirection, false);
	if (viewDirection.z <= 0.0 || !all(isfinite(uv)) || FrameBuffer::IsOutsideFrame(uv))
		return;

	float2 sampleUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv);
	float depth = DepthTexture.SampleLevel(LinearSampler, sampleUV, 0);
#if defined(REFLECTIONS)
	if (FO4Depth::IsFirstPerson(depth))
#else
	if (depth == 1.0 || FO4Depth::IsFirstPerson(depth))
#endif
		return;

	float3 positionView;
	// FO4's infinite far projection needs a finite sky position for upstream capture history.
	if (depth >= 1.0) {
		positionView = viewDirection / viewDirection.z * SharedData::CameraData.x;
	} else {
		float4 projected = mul(float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), FO4Depth::ProjectionDepth(depth, false), 1.0), InvProj);
		positionView = projected.xyz / projected.w;
	}
	if (positionView.z <= 16.5)
		return;

	float3 position = FrameBuffer::ViewToWorld(positionView, false) * 0.001;
	float3 radiance = ColorTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
	// FO4 has no diffuse-only main target; the composite's inputs exclude reflected light.
	if (depth < 1.0) {
		float3 diffuse = DiffuseTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb + TiledDiffuseTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
		radiance = 3.0 * AlbedoTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb * diffuse + EmissiveTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
	}
	if (!all(isfinite(position)) || !all(isfinite(radiance)))
		return;
	CapturePosition[texel] = float4(position, depth >= 1.0 ? 2.0 : 1.0);
	CaptureColor[texel] = float4(clamp(radiance, 0.0, 65504.0), 0);
	CaptureUV[texel] = uv;
}
