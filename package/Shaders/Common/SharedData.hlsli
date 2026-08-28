// gate outermost: inactive emits zero declarations
#ifdef FO4CS_SUBSTRATE
#ifndef __SHARED_DATA_DEPENDENCY_HLSL__
#define __SHARED_DATA_DEPENDENCY_HLSL__

namespace SharedData
{
	cbuffer SharedData : register(b5)
	{
		// x: far, y: near, z: far - near, w: far * near
		float4 CameraData;
		// xy: back-buffer extent, zw: reciprocal extent
		float4 BufferDim;
		// xy: dynamic-resolution ratio, zw: reciprocal ratio
		float4 DynamicResolution;
		// xy: view-ray scale over NDC
		float4 NDCToViewMul;
		// xy: view-ray offset over NDC
		float4 NDCToViewAdd;
		// xyz: world-space direction to the sun, w: 1 when sourced
		float4 SunDirection;
		// Accumulated real seconds since the substrate started
		float Timer;
		float DeltaTime;
		uint FrameCount;
		bool InInterior;
		// xyz: world up in view space, w: 1 when sourced from a valid world camera
		float4 WorldUpView;
	};

	struct ScreenSpaceShadowsSettings
	{
		bool EnableScreenSpaceShadows;
		float ShadowContrast;
		uint2 pad0;
	};

	struct ScreenSpaceGISettings
	{
		bool EnableScreenSpaceGI;
		uint pad0;
		float AoPower;
		float BounceStrength;
	};

	struct WetnessEffectsSettings
	{
		float Wetness;
		float MaxRainWetness;
		float MinRainWetness;
		float pad0;
	};

	cbuffer FeatureData : register(b6)
	{
		ScreenSpaceShadowsSettings screenSpaceShadowsSettings;
		ScreenSpaceGISettings screenSpaceGISettings;
		WetnessEffectsSettings wetnessEffectsSettings;
	};

	// standard D3D depth: near = 0, far = 1
	float GetScreenDepth(float depth)
	{
		return CameraData.w / (-depth * CameraData.z + CameraData.x);
	}

	float4 GetScreenDepths(float4 depths)
	{
		return CameraData.w / (-depths * CameraData.z + CameraData.x);
	}

	float2 ClampDynamicResolutionAdjustedScreenPosition(float2 screenPositionDR, float2 screenPosition)
	{
		float2 minValue = 0.0;
		float2 maxValue = DynamicResolution.xy - 0.5 * BufferDim.zw;
		return clamp(screenPositionDR, minValue, maxValue);
	}

	float2 GetDynamicResolutionAdjustedScreenPosition(float2 uv)
	{
		float2 adjusted = max(0.0, uv * DynamicResolution.xy);
		return ClampDynamicResolutionAdjustedScreenPosition(adjusted, uv);
	}
}
#endif  // __SHARED_DATA_DEPENDENCY_HLSL__
#endif  // FO4CS_SUBSTRATE
