#ifndef FO4_VIEW_POSITION_HLSLI
#define FO4_VIEW_POSITION_HLSLI

#include "FO4/FO4ShaderData.hlsli"

#ifdef FO4CS_SUBSTRATE
namespace FO4GBuffer
{
	float3 GetViewPosition(float2 pixelPosition)
	{
		float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(pixelPosition * SharedData::BufferDim.zw);
		float4 view = mul(FrameBuffer::CameraProjInverse,
			float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), SharedData::GetDepth(uv), 1.0));
		return view.xyz / view.w;
	}
}
#endif
#endif
