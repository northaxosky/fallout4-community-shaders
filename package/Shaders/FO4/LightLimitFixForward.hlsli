#ifndef FO4_LIGHT_LIMIT_FIX_FORWARD_HLSLI
#define FO4_LIGHT_LIMIT_FIX_FORWARD_HLSLI

#include "FO4/LightLimitFixConsumer.hlsli"
#include "FO4/LinearSampler.hlsli"
#include "FO4/Permutation.hlsli"

#include "LightLimitFix/LightLimitFix.hlsli"

namespace FO4LightLimitFix
{
	// FO4: spot cone beside each clustered light, indexed like the light list.
	struct Cone
	{
		float3 direction;
		float cosHalfAngle;
		float exponent;
		uint3 pad;
	};

	StructuredBuffer<Cone> Cones : register(t34);

	// FO4: health-gated; otherwise forward consumers keep native lanes.
	bool UseClusteredLights()
	{
		return FO4SharedData::EnabledLightLimitFix != 0 &&
		       (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld) != 0;
	}

	// FO4: native effect cone term; exponent 0 means no cone.
	float GetConeAttenuation(uint lightIndex, float3 lightPosition, float3 fragmentPosition)
	{
		Cone cone = Cones[lightIndex];
		[branch] if (cone.exponent == 0) return 1.0;
		float cosine = saturate(dot(normalize(fragmentPosition - lightPosition), cone.direction));
		float t = saturate(1.0 - (1.0 - cosine) / (1.0 - cone.cosHalfAngle));
		return min(pow(t, cone.exponent), 1.0);
	}

	bool HasLocalShadow(LightLimitFix::Light light)
	{
		return (light.lightFlags & LightLimitFix::LightFlags::LocalShadow) != 0;
	}

	// FO4: DFLight fades its shadow term to zero toward the local shadow distance.
	float GetLocalShadow(LightLimitFix::Light light, float3 viewPosition, float3 worldPosition, float3 towardLight, float noise)
	{
		float shadow = LightLimitFix::GetLocalShadow(LinearSampler, light.localShadowIndex, worldPosition, FrameBuffer::CameraPosAdjust.xyz, towardLight, false, noise);
		float distanceFade = saturate(dot(viewPosition, viewPosition) / LightLimitFix::LocalShadows[light.localShadowIndex].Params2.z);
		distanceFade *= distanceFade;
		distanceFade *= distanceFade;
		distanceFade *= distanceFade;
		return shadow * (1.0 - distanceFade);
	}

	// FO4: native effect point-light lane, without upstream's pi and 0.5.
	float3 GetEffectLightColor(uint lightIndex, LightLimitFix::Light light, float3 viewPosition, float3 worldPosition)
	{
		float3 lightPosition = light.positionWS.xyz;
		float3 lightDirection = lightPosition - worldPosition;
		float distance = length(lightDirection);
		float x = saturate(distance * light.invRadius);
		float attenuation = GetAttenuation(light, distance, pow(1.0 - x * x, 2.2));
		float3 color = GetLinearColor(light.color, light.lightFlags) * light.fade * attenuation;
		// FO4: the local shadow carries the spot falloff, so the cone is not applied twice.
		[branch] if (HasLocalShadow(light)) return color * GetLocalShadow(light, viewPosition, worldPosition, lightDirection / max(distance, 1e-5), 0.0);
		return color * GetConeAttenuation(lightIndex, lightPosition, worldPosition);
	}

	bool GetClusterLights(float3 viewPosition, out uint lightOffset, out uint lightCount)
	{
		uint clusterIndex = 0;
		lightOffset = 0;
		lightCount = 0;
		if (!LightLimitFix::GetClusterIndex(FrameBuffer::ViewToUV(viewPosition), viewPosition.z, clusterIndex))
			return false;
		lightCount = LightLimitFix::lightGrid[clusterIndex].lightCount;
		lightOffset = LightLimitFix::lightGrid[clusterIndex].offset;
		return true;
	}

	// FO4: a shadow caster lights forward passes only through its cached local shadow.
	bool IsClusterLightSkipped(LightLimitFix::Light light)
	{
		return LightLimitFix::IsLightIgnored(light) || ((light.lightFlags & LightLimitFix::LightFlags::ShadowCaster) != 0 && !HasLocalShadow(light));
	}

	float3 GetEffectLighting(float3 viewPosition, float3 worldPosition, out uint lightCount)
	{
		float3 result = 0.0;
		uint lightOffset;
		GetClusterLights(viewPosition, lightOffset, lightCount);
		[loop] for (uint i = 0; i < lightCount; i++)
		{
			uint clusteredLightIndex = LightLimitFix::lightList[lightOffset + i];
			LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];
			if (IsClusterLightSkipped(light))
				continue;
			result += GetEffectLightColor(clusteredLightIndex, light, viewPosition, worldPosition);
		}
		return result;
	}

	// FO4: 3x the native water specular lane; the caller applies Fresnel.
	float3 GetWaterSpecularLighting(float3 viewPosition, float3 worldPosition, float3 normal, float specularPower, float2 screenPosition)
	{
		float3 viewDirection = normalize(worldPosition);
		float3 result = 0.0;
		uint lightOffset, lightCount;
		GetClusterLights(viewPosition, lightOffset, lightCount);
		[loop] for (uint i = 0; i < lightCount; i++)
		{
			LightLimitFix::Light light = LightLimitFix::lights[LightLimitFix::lightList[lightOffset + i]];
			if (IsClusterLightSkipped(light))
				continue;
			float3 lightDirection = light.positionWS.xyz - worldPosition;
			float distance = length(lightDirection);
			float x = saturate(distance * light.invRadius);
			float attenuation = GetAttenuation(light, distance, 1.0 - x * x);
			float3 towardLight = normalize(lightDirection);
			float shadow = 1.0;
			[branch] if (HasLocalShadow(light)) shadow = GetLocalShadow(light, viewPosition, worldPosition, towardLight, Random::InterleavedGradientNoise(screenPosition, SharedData::FrameCount));
			float3 H = normalize(towardLight - viewDirection);
			result += GetLinearColor(light.color, light.lightFlags) * pow(saturate(dot(H, normal)), specularPower) * light.fade * attenuation * shadow;
		}
		return result * 3;
	}
}
#endif
