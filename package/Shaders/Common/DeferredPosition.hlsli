#ifndef DEFERRED_POSITION_HLSLI_INCLUDED
#define DEFERRED_POSITION_HLSLI_INCLUDED

#include "Common/DepthPartition.hlsli"
#include "FO4/Common/SharedData.hlsli"

namespace DeferredPosition
{
	// FO4 splits scene depth into near/far projection ranges and renders into the dynamic-resolution region.
	bool TryGetViewPositionFromScreenPosition(
		Texture2D<float> sceneDepth,
		float2 pixelPosition,
		float4x4 farReprojection,
		float4x4 nearReprojection,
		out float3 viewPosition)
	{
		viewPosition = 0.0;
		uint2 depthDimensions;
		sceneDepth.GetDimensions(depthDimensions.x, depthDimensions.y);
		if (any(depthDimensions == 0))
			return false;

		uint2 depthPixel = min(uint2(pixelPosition), depthDimensions - 1);
		float rawDepth = sceneDepth.Load(int3(depthPixel, 0));
		bool isNear = DepthPartition::IsNear(rawDepth);
		float2 renderUv =
			pixelPosition * SharedData::BufferDim.zw * SharedData::DynamicResolution.zw;
		float4 position = float4(
			float2(renderUv.x, 1.0 - renderUv.y) * 2.0 - 1.0,
			DepthPartition::ToProjectionDepth(rawDepth, isNear),
			1.0);
		float4 viewPositionH = isNear ?
		                           mul(nearReprojection, position) :
		                           mul(farReprojection, position);
		if (abs(viewPositionH.w) < 1e-6)
			return false;

		viewPosition = viewPositionH.xyz / viewPositionH.w;
		return true;
	}
}

#endif
