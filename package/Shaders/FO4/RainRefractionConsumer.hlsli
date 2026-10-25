#ifndef FO4_RAIN_REFRACTION_CONSUMER
#define FO4_RAIN_REFRACTION_CONSUMER

// FO4: both stages test this one, so their interpolators always match.
#if defined(ENVCUBE_RAIN) && defined(RAIN_REFRACTION)
#	define FO4_RAIN_REFRACTION
#	include "FO4/FO4ShaderData.hlsli"
#endif

// FO4: the vertex stage has no Dynamic Cubemaps define, so only the pixel stage gates on it.
#if defined(FO4_RAIN_REFRACTION) && defined(DYNAMIC_CUBEMAPS) && defined(PSHADER)
#	define FO4_REFRACTIVE_RAIN

#	include "Common/BRDF.hlsli"
#	include "FO4/EffectShadowSampling.hlsli"
#	include "FO4/ForwardPosition.hlsli"
#	if defined(SKYLIGHTING)
#		include "FO4/SkylightingConsumer.hlsli"
#	endif
#	include "FO4/DynamicCubemaps/DynamicCubemaps.hlsli"

Texture2D<float4> TexRaindropNormals : register(t80);

namespace Raindrop
{
	static const float WaterF0 = 0.02;
	static const float Roughness = 0.3;              // Perceptual roughness of the highlight
	static const float TransmissionExponent = 32.0;  // Tightness of a light seen through a drop
}

namespace FO4RainRefraction
{
	struct Surface
	{
		float3 normal;
		float3 view;
		float3 refractDir;  // View ray continued through the drop
	};

	// Light a drop sends toward the viewer from lightColor arriving along L.
	float3 ShadeLight(Surface surface, float3 lightColor, float3 L)
	{
		float3 H = normalize(surface.view + L);
		float NdotL = saturate(dot(surface.normal, L));
		float NdotV = saturate(dot(surface.normal, surface.view));
		float NdotH = saturate(dot(surface.normal, H));
		float VdotH = saturate(dot(surface.view, H));
		float3 specular = BRDF::D_GGX(Raindrop::Roughness, NdotH) * BRDF::Vis_SmithJointApprox(Raindrop::Roughness, NdotV, NdotL) *
		                  BRDF::F_Schlick(Raindrop::WaterF0, VdotH) * NdotL;

		// A light behind the drop shows along the refracted ray; normalized Phong lobe
		float transmissionLobe = pow(saturate(dot(surface.refractDir, L)), Raindrop::TransmissionExponent) * (Raindrop::TransmissionExponent + 2.0) / Math::TAU;
		float3 transmission = transmissionLobe * (1.0 - BRDF::F_Schlick(Raindrop::WaterF0, NdotV));

		return ColorManagement::SceneToLinear(lightColor) * (specular + transmission);
	}

	// Sun color at positionWS after directional and world shadows.
	float3 GetDirLightColor(float3 positionWS)
	{
		float unusedDetailedShadow;
		float3 dirLightColor = SharedData::DirLightColor.xyz * ShadowSampling::GetLightingShadow(positionWS, unusedDetailedShadow);
		// Menu and reflection passes don't match the world heightmap
		if (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld)
			dirLightColor *= ShadowSampling::GetWorldShadow(positionWS, FrameBuffer::CameraPosAdjust.xyz);
		return dirLightColor;
	}

	// FO4: the effect's base sampler stands in for upstream's SampSourceTexture.
	float4 Shade(float3 screenPosition, float2 raindropUV, bool frontFace, SamplerState dropSampler)
	{
		float4 raindropNormal = TexRaindropNormals.Sample(dropSampler, raindropUV);
		float alpha = saturate(raindropNormal.w * (1.0 - SharedData::rainRefractionSettings.MotionTransparency));
		clip(alpha - (4.0 / 255.0));
		raindropNormal.y = 1.0 - raindropNormal.y;

		// FO4: position rebuilds from SV_POSITION; camera at origin.
		float3 posWS = FO4Forward::WorldPosition(screenPosition);

		// World TBN from screen derivatives; the camera-aligned billboard spans X/Y
		float3 T = normalize(ddx(posWS));
		float3 N = normalize(cross(T, -ddy(posWS)));
		float3 B = cross(N, T);
		float3x3 TBN = float3x3(T, B, N);

		float3 normalTS = normalize(raindropNormal.xyz * 2.0 - 1.0);
		float3 normalWS = normalize(mul(normalTS, TBN));

		if (frontFace)
			normalWS = -normalWS;

		float3 V = normalize(-posWS);
		float NdotV = saturate(dot(normalWS, V));
		float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

		float3 reflectDir = reflect(-V, normalWS);
		float3 refractDir = refract(-V, normalWS, 1.0 / 1.33);
		if (dot(refractDir, refractDir) < 1e-4)
			refractDir = -V;

		float3 reflectColor = ColorManagement::SceneToLinear(DynamicCubemaps::EnvReflectionsTexture.SampleLevel(dropSampler, reflectDir, 0).xyz);
		float3 refractColor = ColorManagement::SceneToLinear(DynamicCubemaps::EnvReflectionsTexture.SampleLevel(dropSampler, refractDir, 0).xyz);

		// The env cubemap carries faint bounce light, so lights shade the drop directly
		// FO4: point lights await Light Limit Fix.
		Surface drop = { normalWS, V, refractDir };
		float3 directLighting = ShadeLight(drop, GetDirLightColor(posWS), SharedData::DirLightDirection.xyz);

		return float4(ColorManagement::LinearToScene(lerp(refractColor, reflectColor, fresnel) + directLighting), alpha);
	}
}
#endif
#endif
