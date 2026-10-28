// SPDX-License-Identifier: GPL-3.0-only
#include "FO4/WorldShadowConsumer.hlsli"

#if defined(CLOUD_SKY_OVERLAY) || defined(CLOUD_LAYERS)
#	ifdef CLOUD_LAYERS
TextureCube<float4> CloudLayerTexture : register(t26);
#	endif
cbuffer Overlay : register(b8)
{
	// Direction to the visible sun disc; w is zero when it is unavailable.
	float4 VisibleSun;
};

// Thin ring of the given angular radius around a direction.
bool OnRing(float3 direction, float3 center, float radius)
{
	return abs(degrees(acos(saturate(dot(direction, center)))) - radius) < 0.2;
}
#endif

// Drawn over the final image, so the value is not tonemapped or fogged.
float4 main(float4 screenPosition : SV_Position) : SV_Target
{
	uint2 size;
	SharedData::DepthTexture.GetDimensions(size.x, size.y);
	float2 uv = screenPosition.xy / float2(size);
	float depth = SharedData::DepthTexture.Load(int3(uint2(screenPosition.xy), 0)).x;
	float4 view = mul(FrameBuffer::CameraProjUnjitteredInverse,
		float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
	// Same camera-relative position the receivers pass to GetWorldShadow.
	float3 worldPosition = FrameBuffer::ViewToWorld(view.xyz / view.w);
#if defined(CLOUD_SKY_OVERLAY) || defined(CLOUD_LAYERS)
	float3 direction = normalize(worldPosition);
	// Green: receiver sun direction. Yellow: the visible sun disc.
	if (OnRing(direction, normalize(SharedData::DirLightDirection.xyz), 1.5))
		return float4(0.0, 1.0, 0.0, 1.0);
	if (VisibleSun.w > 0.0 && OnRing(direction, VisibleSun.xyz, 2.5))
		return float4(1.0, 1.0, 0.0, 1.0);
#	ifdef CLOUD_LAYERS
	// Premultiplied color accumulates; alpha holds the coverage.
	float4 layers = CloudLayerTexture.SampleLevel(LinearSampler, direction, 0);
	float coverage = saturate(layers.a);
	return float4(coverage > 0.0 ? layers.rgb / coverage : 0.0, coverage * 0.85);
#	else
	float occlusion = CloudShadows::CloudShadowsTexture.SampleLevel(LinearSampler, direction, 0);
	return float4(1.0, 0.0, 0.0, occlusion);
#	endif
#else
	float shadow = depth < 1.0 ? CloudShadows::GetCloudShadowMult(worldPosition, LinearSampler) : 0.0;
	return float4(shadow.xxx, 1.0);
#endif
}
