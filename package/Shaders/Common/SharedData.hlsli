// The substrate gate is outermost on purpose: without FO4CS_SUBSTRATE this file declares nothing.
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
		bool EnableAmbientPass;
		float AoPower;
		float BounceStrength;
	};

	struct WetnessEffectsSettings
	{
		bool EnableWetnessEffects;
		bool EnableAmbientPass;
		float Wetness;
		float pad0;
	};

	cbuffer FeatureData : register(b6)
	{
		ScreenSpaceShadowsSettings screenSpaceShadowsSettings;
		ScreenSpaceGISettings screenSpaceGISettings;
		WetnessEffectsSettings wetnessEffectsSettings;
	};

	// Fallout 4 uses standard D3D depth, so near maps to 0 and far to 1.
	float GetScreenDepth(float depth)
	{
		return CameraData.w / (-depth * CameraData.z + CameraData.x);
	}

	float4 GetScreenDepths(float4 depths)
	{
		return CameraData.w / (-depths * CameraData.z + CameraData.x);
	}

	// Remaps a full-resolution uv onto the region the engine actually rendered.
	float2 GetDynamicResolutionAdjustedScreenPosition(float2 uv)
	{
		float2 adjusted = max(0.0, uv * DynamicResolution.xy);
		return min(DynamicResolution.xy - 0.5 * BufferDim.zw, adjusted);
	}
}
#endif  // __SHARED_DATA_DEPENDENCY_HLSL__
#endif  // FO4CS_SUBSTRATE
