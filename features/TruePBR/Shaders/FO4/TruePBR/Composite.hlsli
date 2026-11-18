#ifndef FO4_TRUE_PBR_COMPOSITE_HLSLI
#define FO4_TRUE_PBR_COMPOSITE_HLSLI

#include "FO4/AmbientConsumer.hlsli"
#include "FO4/GBuffer.hlsli"
#include "FO4/TruePBR/Lighting.hlsli"
#include "FO4/ViewPosition.hlsli"
#ifdef SSGI
#	include "FO4/ScreenSpaceGIConsumer.hlsli"
#endif
#ifdef DYNAMIC_CUBEMAPS
#	include "FO4/DynamicCubemaps/Composite.hlsli"
#endif

namespace FO4TruePBR
{
	// Whole-color SSAO would also darken direct light and emission.
	float3 Resolve(float2 a_pixelPosition, float3 a_color, float a_ssao, SamplerState a_sampler)
	{
		const int3 texel = int3(int2(a_pixelPosition), 0);
		const Surface surface = Decode(a_pixelPosition, FO4GBufferViews::Material.Load(texel));
		const float3 normalView = FO4GBuffer::DecodeViewNormal(FO4GBufferViews::Normal.Load(texel).xy);
		const float3 viewPosition = FO4GBuffer::GetViewPosition(a_pixelPosition);
		const float3 viewDirection = normalize(-viewPosition);
		const float3 base = FO4GBufferViews::Albedo.Load(texel).xyz;
		const float vertexAOStore = FO4GBufferViews::Emissive.Load(texel).w;
		const float NdotV = saturate(dot(normalView, viewDirection));
		const IndirectLobeWeights lobes = IndirectLobes(surface, base, normalView, viewDirection);
		const float alpha = surface.roughness * surface.roughness;

#ifdef SSGI
		// SSGI's own visibility already occludes diffuse and environment.
		const float ssao = ScreenSpaceGI::Enabled ? 1.0 : a_ssao;
#else
		const float ssao = a_ssao;
#endif

		const float3 diffuseAlbedo = lobes.diffuse;
		const float3 normalWorld = normalize(FrameBuffer::ViewToWorld(normalView, false));
		const float3 ambient = FO4Ambient::DiffuseAlbedo(a_pixelPosition, normalView, normalWorld, diffuseAlbedo, 1.0 - vertexAOStore);
		float3 color = FO4Ambient::SplitOcclusion(a_color, ambient, MultiBounceAO(diffuseAlbedo, ssao), 1.0);

#ifdef DYNAMIC_CUBEMAPS
		DynamicCubemaps::Surface environmentSurface;
		environmentSurface.positionView = viewPosition;
		environmentSurface.normalView = normalView;
		const DynamicCubemaps::Environment environment = DynamicCubemaps::SampleEnvironment(environmentSurface, surface.roughness, a_sampler);
		float3 radiance = environment.environment + environment.sky;
#	ifdef SSGI
		radiance = ScreenSpaceGI::ComposeSpecular(a_pixelPosition, (float3x3)FrameBuffer::CameraViewInverse, viewDirection, radiance, 1.0 - surface.roughness, vertexAOStore);
#	else
		radiance *= SpecularOcclusion(NdotV, alpha, ssao);
#	endif
		color += lobes.specular * radiance;
#endif
		return color;
	}
}
#endif
