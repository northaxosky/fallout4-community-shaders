#ifndef FO4_SKYLIGHTING_CONSUMER
#define FO4_SKYLIGHTING_CONSUMER

#include "FO4/FO4ShaderData.hlsli"

#ifdef SKYLIGHTING
// FO4: pixel and compute lighting read the probe array at the same slot.
#	define SKYLIGHTING_PROBE_REGISTER t50
#	include "Common/Color.hlsli"
#	include "Skylighting/Skylighting.hlsli"
#endif

namespace Skylighting
{
#ifdef SKYLIGHTING
	Texture2D<float4> AlbedoTexture : register(t51);
	// FO4: MRT4 alpha stores 1 - vertexAO, upstream's Masks2.x.
	Texture2D<float4> VertexAOTexture : register(t52);

	// FO4: ambient is a separate linear addend and b6 makes the gamma pair the identity,
	// so upstream's ApplySkylighting subtract and clamp collapse to this multiplier.
	float3 GetAmbientScale(float3 viewPosition, float3 normalView, float3 albedo, float vertexAO)
	{
		float3 positionMS = FrameBuffer::ViewToWorld(viewPosition);
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normalView, false));
		sh2 skylightingSH = Sample(positionMS, normalWS);
		float skylightingDiffuse = GetSkylightingDiffuse(skylightingSH, positionMS, normalWS, vertexAO);
		return MultiBounceAO(albedo, skylightingDiffuse);
	}

	float3 GetAmbientScale(float2 pixelPosition, float3 viewPosition, float3 normalView)
	{
		int3 texel = int3(int2(pixelPosition), 0);
		return GetAmbientScale(viewPosition, normalView, AlbedoTexture.Load(texel).xyz, 1.0 - VertexAOTexture.Load(texel).w);
	}

	float3 GetViewPosition(float2 pixelPosition)
	{
		float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(pixelPosition * SharedData::BufferDim.zw);
		float4 view = mul(FrameBuffer::CameraProjInverse,
			float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), SharedData::GetDepth(uv), 1.0));
		return view.xyz / view.w;
	}

	// FO4: lighting stages evaluate the ambient gradient at several directions per pixel.
	static float3 AmbientScale = 1.0;
#endif

#ifdef SKYLIGHTING_FULLSCREEN_DEBUG
	// FO4: composite families share no normal input; a producer isolates visibility.
	bool TryGetDebugColor(float2 pixelPosition, out float4 color)
	{
		color = 0.0;
		if (FO4SharedData::DebugOwner != FullscreenDebugOwner::Skylighting || FO4SharedData::DebugMode == 0)
			return false;
		uint2 dimensions;
		FO4SharedData::DebugTexture.GetDimensions(dimensions.x, dimensions.y);
		if (any(dimensions == 0))
			return false;
		color = FO4SharedData::DebugTexture.Load(int3(min(uint2(pixelPosition), dimensions - 1), 0));
		return true;
	}
#endif
}

// Lighting call sites stay declarative and compile away without the feature.
#ifdef SKYLIGHTING
#	define FO4_AMBIENT_SKYLIGHTING_SET(pixelPosition, viewPosition, normalView) \
		Skylighting::AmbientScale = Skylighting::GetAmbientScale(pixelPosition, viewPosition, normalView)
#	define FO4_AMBIENT_SKYLIGHTING(ambient) ((ambient) * Skylighting::AmbientScale)
#else
#	define FO4_AMBIENT_SKYLIGHTING_SET(pixelPosition, viewPosition, normalView)
#	define FO4_AMBIENT_SKYLIGHTING(ambient) (ambient)
#endif
#endif
