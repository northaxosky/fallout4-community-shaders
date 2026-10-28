#ifndef FO4_SKYLIGHTING_CONSUMER
#define FO4_SKYLIGHTING_CONSUMER

#include "FO4/FO4ShaderData.hlsli"
#include "FO4/SunShadowFamilies.hlsli"

#ifdef SKYLIGHTING
// FO4: directional families with sun lobes read visibility at t53.
#	if defined(FO4_SUN_SHADOWED_FAMILY) || (defined(DIRECTIONAL) && defined(BSDFLIGHT_PS_UNSHADOWED))
#		define SKYLIGHTING_SHADOW_VIS
#	endif
// FO4: pixel and compute lighting read the probe array at the same slot.
#	define SKYLIGHTING_PROBE_REGISTER t50
#	include "Common/Color.hlsli"
#	include "Skylighting/Skylighting.hlsli"
#endif

namespace Skylighting
{
#ifdef IBL
	// FO4: both scalars of one probe sample at a composite probe site.
	struct SpecularVisibility
	{
		float specular;
		float visibility;
	};

	SpecularVisibility Unoccluded()
	{
		SpecularVisibility result;
		result.specular = 1.0;
		result.visibility = 1.0;
		return result;
	}
#	define FO4_SPECULAR_UNOCCLUDED Skylighting::Unoccluded()
#else
	// FO4: only IBL reads the cosine visibility, so other builds keep the scalar.
	typedef float SpecularVisibility;
#	define FO4_SPECULAR_UNOCCLUDED 1.0
#endif

#ifdef SKYLIGHTING
	Texture2D<float4> AlbedoTexture : register(t51);
	// FO4: MRT4 alpha stores 1 - vertexAO, upstream's Masks2.x.
	Texture2D<float4> VertexAOTexture : register(t52);

	// FO4: shadow visibility of the last SampleShadowVisibility; lobes share it.
	static float ShadowVisibility = 1.0;

	sh2 SampleProbes(float3 positionMS, float3 normalWS)
	{
#	ifdef SKYLIGHTING_SHADOW_VIS
		// Upstream samples shadow visibility separately, height clamped.
		float unusedVisibility;
		return Sample(positionMS, normalWS, unusedVisibility, false);
#	else
		return Sample(positionMS, normalWS);
#	endif
	}

	// FO4: scalar diffuse visibility, the input of the ambient scale.
	float GetDiffuseVisibility(float3 positionMS, float3 normalWS, float vertexAO)
	{
		sh2 skylightingSH = SampleProbes(positionMS, normalWS);
		return GetSkylightingDiffuse(skylightingSH, positionMS, normalWS, vertexAO);
	}

	// Upstream's composite takes the faded cosine lobe, without vertex AO.
	float GetCompositeDiffuse(float3 positionMS, float3 normalWS)
	{
		// FO4: Skylighting toggles live; disabled probes are unoccluded.
		[branch] if (FO4SharedData::EnabledSkylighting == 0) return 1.0;
		return EvaluateDiffuse(SampleProbes(positionMS, normalWS), normalWS, GetFadeOutFactor(positionMS));
	}

	// FO4: ambient is a linear addend and gamma is the identity, so scale it.
	float3 GetAmbientScale(float3 viewPosition, float3 normalView, float3 albedo, float vertexAO)
	{
		[branch] if (FO4SharedData::EnabledSkylighting == 0) return 1.0;
		float3 positionMS = FrameBuffer::ViewToWorld(viewPosition);
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normalView, false));
		return MultiBounceAO(albedo, GetDiffuseVisibility(positionMS, normalWS, vertexAO));
	}

	float3 GetAmbientScale(float2 pixelPosition, float3 viewPosition, float3 normalView)
	{
		[branch] if (FO4SharedData::EnabledSkylighting == 0) return 1.0;
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

	// FO4: composite probe sites share upstream's lobe; positions are view space.
	SpecularVisibility GetSpecularVisibility(float3 viewPosition, float3 normalView, float glossiness)
	{
		// FO4: the interior branch samples EnvTexture and never reads visibility.
		[branch] if (SharedData::InInterior || FO4SharedData::EnabledSkylighting == 0) return FO4_SPECULAR_UNOCCLUDED;
		float3 positionMS = FrameBuffer::ViewToWorld(viewPosition);
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normalView, false));
		float3 V = -normalize(positionMS);
		float3 R = reflect(-V, normalWS);
		sh2 skylightingSH = SampleProbes(ClampToArrayHeight(positionMS), R);
		float fadeOut = GetHorizontalFadeOutFactor(positionMS);
#	ifdef IBL
		SpecularVisibility result;
		result.specular = EvaluateSpecular(skylightingSH, SphericalHarmonics::FauxSpecularLobe(normalWS, V, 1.0 - glossiness), fadeOut);
		result.visibility = EvaluateVisibility(skylightingSH, fadeOut);
		return result;
#	else
		return EvaluateSpecular(skylightingSH, SphericalHarmonics::FauxSpecularLobe(normalWS, V, 1.0 - glossiness), fadeOut);
#	endif
	}

	// FO4: lighting evaluates the ambient gradient at several directions per pixel.
	static float3 AmbientScale = 1.0;

#	ifdef SKYLIGHTING_SHADOW_VIS
	void SampleShadowVisibility(float3 viewPosition, float3 normalView)
	{
		[branch] if (FO4SharedData::EnabledSkylighting == 0) return;
		float3 positionMS = FrameBuffer::ViewToWorld(viewPosition);
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normalView, false));
		// Height clamped so visibility stays valid above the volume, unlike the diffuse fade.
		Sample(ClampToArrayHeight(positionMS), normalWS, ShadowVisibility);
	}

#	endif
#endif

#ifdef SKYLIGHTING_FULLSCREEN_DEBUG
	// FO4: composite families share no normal input; a producer isolates it.
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
#	define FO4_SKYLIGHTING_SPECULAR(viewPosition, normalView, glossiness) Skylighting::GetSpecularVisibility(viewPosition, normalView, glossiness)
#else
#	define FO4_AMBIENT_SKYLIGHTING_SET(pixelPosition, viewPosition, normalView)
#	define FO4_AMBIENT_SKYLIGHTING(ambient) (ambient)
#	define FO4_SKYLIGHTING_SPECULAR(viewPosition, normalView, glossiness) FO4_SPECULAR_UNOCCLUDED
#endif

// Sun lobes use probe visibility where upstream's SHADOW_VIS does.
#ifdef SKYLIGHTING_SHADOW_VIS
#	define FO4_SKYLIGHTING_SOFT_SHADOW(softShadow, viewPosition, normalView) \
		Skylighting::SampleShadowVisibility(viewPosition, normalView);        \
		float softShadow = Skylighting::ShadowVisibility
#endif
#endif
