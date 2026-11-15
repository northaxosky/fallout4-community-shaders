#ifndef FO4_TRUE_PBR_COMPOSITE_HLSLI
#define FO4_TRUE_PBR_COMPOSITE_HLSLI

#include "FO4/AmbientConsumer.hlsli"
#include "FO4/TruePBR/Lighting.hlsli"
#ifdef SSGI
#	include "FO4/ScreenSpaceGIConsumer.hlsli"
#endif
#ifdef DYNAMIC_CUBEMAPS
#	include "FO4/DynamicCubemaps/Composite.hlsli"
#endif

namespace FO4TruePBR
{
	float3 DecodeNormal(float2 a_encoded)
	{
		const float2 encoded = a_encoded * 4.0 - 2.0;
		const float lengthSquared = dot(encoded, encoded);
		return float3(encoded * sqrt(1.0 - lengthSquared * 0.25), -(1.0 - lengthSquared * 0.5));
	}

	// Replaces the native whole-color SSAO of a TruePBR pixel. Material AO is in the lobe weights;
	// SSAO occludes the ambient diffuse and the environment radiance, not direct light or emission.
	// a_color is the lit color: direct, ambient diffuse, specular and emission.
	float3 Resolve(float2 a_pixelPosition, float3 a_color, float a_ssao, SamplerState a_sampler)
	{
		const int3 texel = int3(int2(a_pixelPosition), 0);
		const Surface surface = Decode(a_pixelPosition, FO4GBufferViews::Material.Load(texel));
		const float3 normalView = DecodeNormal(FO4GBufferViews::Normal.Load(texel).xy);
		const float3 viewPosition = Skylighting::GetViewPosition(a_pixelPosition);
		const float3 viewDirection = normalize(-viewPosition);
		const float3 base = FO4GBufferViews::Albedo.Load(texel).xyz;
		const float vertexAOStore = FO4GBufferViews::Emissive.Load(texel).w;
		const float NdotV = saturate(dot(normalView, viewDirection));
		const float3 specularAlbedo = PBR::SpecularDirectionalAlbedo(surface.f0, surface.roughness, NdotV);
		const float alpha = surface.roughness * surface.roughness;

#ifdef SSGI
		// SSGI's own visibility already occludes the diffuse and the environment.
		const float ssao = ScreenSpaceGI::Enabled ? 1.0 : a_ssao;
#else
		const float ssao = a_ssao;
#endif

		const float3 diffuseAlbedo = base * IndirectDiffuseThroughput(surface, base, normalView, viewDirection);
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
		color += specularAlbedo * SpecularOcclusion(NdotV, alpha, surface.ao) * radiance;
#endif
		return color;
	}
}
#endif
