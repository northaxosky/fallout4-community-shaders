#ifndef FO4_SSGI_CONSUMER
#define FO4_SSGI_CONSUMER

#include "Common/Color.hlsli"
#include "Common/Shading.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#include "FO4/FO4ShaderData.hlsli"
#include "FO4/ScreenSpaceGI/Contracts.hlsli"
#include "FO4/SkylightingConsumer.hlsli"

namespace ScreenSpaceGI
{
	Texture2D<float> OcclusionTexture : register(t26);
	Texture2D<float4> BounceLumaTexture : register(t27);
	Texture2D<float2> BounceChromaTexture : register(t28);
	Texture2D<float4> NormalTexture : register(t29);
	Texture2D<float4> SpecularTexture : register(t38);

	// Upstream d330bf12d DeferredCompositeCS.hlsl:46-56,123-180.
	float3 ComposeDiffuse(float2 screenPosition, float3x3 viewToWorld,
		float3 albedo, float3 diffuseColor, float vertexAOStore)
	{
		if (!Enabled)
			return diffuseColor;
		int3 texel = int3(int2(screenPosition), 0);
		float3 normalVS = GBuffer::DecodeNormal(NormalTexture.Load(texel).xy);
		float3 normalWS = normalize(mul(viewToWorld, normalVS));
		float ssgiAo = 1 - OcclusionTexture.Load(texel);
		// FO4: the native emissive attachment alpha supplies upstream Masks2.x.
		float vertexAO = 1.0 - vertexAOStore;
		ssgiAo = saturate(ssgiAo / max(vertexAO, EPSILON_DIVISION));
		float4 ssgiIlYSh = BounceLumaTexture.Load(texel);
		float ssgiIlY = SphericalHarmonics::SHHallucinateZH3Irradiance(ssgiIlYSh, normalWS);
		float2 ssgiIlCoCg = BounceChromaTexture.Load(texel);
		float3 ssgiIl = max(0, Color::YCoCgToRGB(float3(ssgiIlY, ssgiIlCoCg)));
		// FO4: native HDR/albedo are linear; convert once at the upstream colour boundary.
		float3 linAlbedo = albedo;
		float3 multiBounceSSGIAo = MultiBounceAO(linAlbedo, ssgiAo);
		// FO4: native diffuse folds in powered DALC without an upstream Masks.z channel.
		float3 directionalAmbientColor = FO4SharedData::GetAmbientLinear(normalWS) * albedo;
#ifdef SKYLIGHTING
		// FO4: lighting scaled the ambient addend; scale the separated term too.
		directionalAmbientColor *= Skylighting::GetAmbientScale(Skylighting::GetViewPosition(screenPosition), normalVS, albedo, vertexAO);
#endif
		float maxScale = 1.0;
		if (directionalAmbientColor.x > 0.0)
			maxScale = min(maxScale, diffuseColor.x / directionalAmbientColor.x);
		if (directionalAmbientColor.y > 0.0)
			maxScale = min(maxScale, diffuseColor.y / directionalAmbientColor.y);
		if (directionalAmbientColor.z > 0.0)
			maxScale = min(maxScale, diffuseColor.z / directionalAmbientColor.z);
		directionalAmbientColor *= maxScale;
		diffuseColor = max(0.0, diffuseColor - directionalAmbientColor);
		diffuseColor *= sqrt(multiBounceSSGIAo);
		diffuseColor += directionalAmbientColor * multiBounceSSGIAo;
		diffuseColor += ssgiIl * linAlbedo;
		return diffuseColor;
	}

	// Upstream d330bf12d DeferredCompositeCS.hlsl:58-76,273-289.
	float3 ComposeSpecular(float2 screenPosition, float3x3 viewToWorld,
		float3 viewVS, float3 irradiance, float glossiness, float vertexAOStore)
	{
		if (!Enabled)
			return irradiance;
		int3 texel = int3(int2(screenPosition), 0);
		float3 normalVS = GBuffer::DecodeNormal(NormalTexture.Load(texel).xy);
		float3 normalWS = normalize(mul(viewToWorld, normalVS));
		float3 viewWS = normalize(mul(viewToWorld, viewVS));
		float roughness = 1.0 - glossiness;
		sh2 lobe = SphericalHarmonics::FauxSpecularLobe(normalWS, viewWS, roughness);
		float ao = saturate((1.0 - OcclusionTexture.Load(texel)) / max(1.0 - vertexAOStore, EPSILON_DIVISION));
		ao = SpecularOcclusion(saturate(dot(normalWS, viewWS)), roughness * roughness, ao);
		float ilY = SphericalHarmonics::FuncProductIntegral(BounceLumaTexture.Load(texel), lobe);
		float2 ilCoCg = BounceChromaTexture.Load(texel);
		float3 il = max(0, Color::YCoCgToRGB(float3(ilY, ilCoCg / Math::PI)));
		float4 hq = SpecularTexture.Load(texel);
		ao *= 1.0 - hq.a;
		il += hq.rgb;
		irradiance *= ao;
		il = Color::RGBToYCoCg(il);
		if (il.x > 0.0)
			il = max(0, Color::YCoCgToRGB(float3(il.x, lerp(il.yz, Color::RGBToYCoCg(irradiance).yz, 0.5))));
		else
			il = 0;
		return irradiance + il;
	}
}
#endif
