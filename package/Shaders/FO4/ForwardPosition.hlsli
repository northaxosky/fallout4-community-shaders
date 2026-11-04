#ifndef FO4_FORWARD_POSITION_HLSLI
#define FO4_FORWARD_POSITION_HLSLI

#include "FO4/Depth.hlsli"
#include "FO4/FO4ShaderData.hlsli"

namespace FO4Forward
{
	float2 ScreenUV(float3 screenPosition)
	{
		return FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(
			screenPosition.xy * SharedData::BufferDim.zw);
	}

	float3 Unproject(float2 uv, float depth)
	{
		float4 view = mul(FrameBuffer::CameraProjInverse,
			float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
		return view.xyz / view.w;
	}

	// First-person pixels use scene depth; the world camera cannot project them.
	float3 ViewPosition(float3 screenPosition)
	{
		float2 uv = ScreenUV(screenPosition);
		float depth = FO4Depth::ProjectionDepth(screenPosition.z);
		if (FO4Depth::IsFirstPerson(screenPosition.z))
			depth = SharedData::GetDepth(uv);
		return Unproject(uv, depth);
	}

	// Unit camera-to-fragment ray; no depth read, any depth lies on it.
	float3 ViewDirection(float3 screenPosition)
	{
		return normalize(Unproject(ScreenUV(screenPosition), 0.5));
	}

	// Camera-relative world position, upstream's input.WorldPosition.
	float3 WorldPosition(float3 screenPosition)
	{
		return FrameBuffer::ViewToWorld(ViewPosition(screenPosition));
	}
}
#endif
