#ifndef FO4_FORWARD_POSITION_HLSLI
#define FO4_FORWARD_POSITION_HLSLI

#include "FO4/Depth.hlsli"
#include "FO4/FO4ShaderData.hlsli"

namespace FO4Forward
{
	// First-person pixels use scene depth; the world camera cannot project them.
	float3 ViewPosition(float3 screenPosition)
	{
		float2 uv = screenPosition.xy * SharedData::BufferDim.zw;
		uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(uv);
		float depth = FO4Depth::ProjectionDepth(screenPosition.z);
		if (FO4Depth::IsFirstPerson(screenPosition.z))
			depth = SharedData::GetDepth(uv);
		float4 view = mul(FrameBuffer::CameraProjInverse,
			float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
		return view.xyz / view.w;
	}

	// Camera-relative world position, upstream's input.WorldPosition.
	float3 WorldPosition(float3 screenPosition)
	{
		return FrameBuffer::ViewToWorld(ViewPosition(screenPosition));
	}
}
#endif
