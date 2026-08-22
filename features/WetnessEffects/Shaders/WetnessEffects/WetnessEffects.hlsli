#ifndef __WETNESS_EFFECTS_DEPENDENCY_HLSL__
#define __WETNESS_EFFECTS_DEPENDENCY_HLSL__

#include "Common/SharedData.hlsli"

namespace WetnessEffects
{
#ifdef WETNESS_COMPOSITE_CONSUMER
	// authoritative engine G-buffer normal, rebound per injected composite draw
	Texture2D<float4> GbufferNormal : register(t25);
#endif

	static const float FilmF0 = 0.02;
	static const float MinFilmRoughness = 0.05;
	static const float MaxFilmSpecularMagnitude = 15.0;
	// FO4's six reconstructed BSDFLight families normalize traditional specular with pi
	static const float FilmSpecularScale = 3.1415927;
	static const float DotClampEpsilon = 1e-5;
	static const float LengthSquaredEpsilon = 1e-8;

	// view-space rain-facing wetness; zero disables every wetness term downstream
	float GetWetness(float3 normalView, float4 worldUpView)
	{
		float wetness = 0.0;
		[branch] if (SharedData::wetnessEffectsSettings.Wetness > 0.0
			&& worldUpView.w == 1.0) {
			float worldUp = dot(normalView, worldUpView.xyz);
			float rainFacing = saturate(
				max(SharedData::wetnessEffectsSettings.MinRainWetness, worldUp));
			wetness = SharedData::wetnessEffectsSettings.Wetness * rainFacing *
				SharedData::wetnessEffectsSettings.MaxRainWetness;
		}
		return wetness;
	}

	float FilmRoughness(float wetness)
	{
		return max(saturate(1.0 - wetness), MinFilmRoughness);
	}

	float FilmStrength(float filmRoughness)
	{
		return saturate(1.0 - filmRoughness);
	}

#ifdef WETNESS_COMPOSITE_CONSUMER
	struct Surface
	{
		float3 normalView;
		float wetness;
	};

	// t25 outside the prepass encode domain (including an explicit null bind) is identity
	Surface GetSurface(float2 screenPosition, float4 worldUpView)
	{
		Surface surface;
		surface.normalView = float3(0.0, 0.0, -1.0);
		surface.wetness = 0.0;

		float2 encoded =
			GbufferNormal.Load(int3(int2(screenPosition), 0)).xy * 4.0 - 2.0;
		float encodedLengthSquared = dot(encoded, encoded);
		// a NaN encoding fails this test and keeps the identity surface
		[branch] if (encodedLengthSquared <= 4.0) {
			surface.normalView = float3(
				encoded * sqrt(1.0 - encodedLengthSquared * 0.25),
				-(1.0 - encodedLengthSquared * 0.5));
			surface.wetness = GetWetness(surface.normalView, worldUpView);
		}
		return surface;
	}
#endif

	// upstream substrate darkening with material porosity fixed at 1
	float3 WetAlbedo(float3 baseColor, float wetness)
	{
		float3 wetColor = baseColor;
		[branch] if (wetness > 0.0) {
			float albedoAmount = wetness * wetness;
			wetColor = lerp(
				baseColor, pow(abs(baseColor), 1.0 + albedoAmount), 0.5);
		}
		return wetColor;
	}

	float D_GGX(float roughness, float NdotH)
	{
		float a = roughness * roughness;
		float a2 = a * a;
		float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
		return a2 / (3.1415927 * d * d);
	}

	float Vis_SmithJointApprox(float roughness, float NdotV, float NdotL)
	{
		float a = roughness * roughness;
		float visSmithV = NdotL * (NdotV * (1.0 + a) + a);
		float visSmithL = NdotV * (NdotL * (1.0 + a) + a);
		return 0.5 / max(visSmithV + visSmithL, 1e-6);
	}

	float F_Schlick(float f0, float VdotH)
	{
		float fc = pow(1.0 - VdotH, 5);
		return fc + (1.0 - fc) * f0;
	}

	// [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
	float2 EnvBRDF(float roughness, float NdotV)
	{
		const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
		const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
		float4 r = roughness * c0 + c1;
		float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
		return float2(-1.04, 1.04) * a004 + r.zw;
	}

	// per-light water film: attenuates the native lobes and adds its own GGX lobe
	void ApplyDirectCoat(
		float3 normalView,
		float3 viewDir,
		float3 lightDir,
		float3 lightColor,
		float wetness,
		inout float3 diffuse,
		inout float3 specular)
	{
		[branch] if (wetness > 0.0) {
			float roughness = FilmRoughness(wetness);
			float strength = FilmStrength(roughness);

			float3 halfVector = viewDir + lightDir;
			halfVector *= rsqrt(
				max(dot(halfVector, halfVector), LengthSquaredEpsilon));

			float NdotL = clamp(dot(normalView, lightDir), DotClampEpsilon, 1.0);
			float NdotV = saturate(abs(dot(normalView, viewDir)) + DotClampEpsilon);
			float NdotH = saturate(dot(normalView, halfVector));
			float VdotH = saturate(dot(viewDir, halfVector));

			float D = D_GGX(roughness, NdotH);
			float G = Vis_SmithJointApprox(roughness, NdotV, NdotL);
			float fresnel = F_Schlick(FilmF0, VdotH);
			float filmFresnel = fresnel * strength;
			float filmBrdf = min(D * G * fresnel, MaxFilmSpecularMagnitude);

			float3 film =
				filmBrdf * strength * NdotL * lightColor * FilmSpecularScale;

			diffuse *= 1.0 - filmFresnel;
			specular *= 1.0 - filmFresnel;
			specular += film;
		}
	}

	// upstream GetWetnessIndirectLobeWeights: environment-BRDF weighted film Fresnel
	float GetEnvironmentFilmWeight(float3 normalView, float3 viewDir, float wetness)
	{
		float weight = 0.0;
		[branch] if (wetness > 0.0) {
			float roughness = FilmRoughness(wetness);
			float NdotV = saturate(abs(dot(normalView, viewDir)) + DotClampEpsilon);
			float2 environmentBRDF = EnvBRDF(roughness, NdotV);
			weight = (FilmF0 * environmentBRDF.x + environmentBRDF.y) *
				FilmStrength(roughness);
		}
		return weight;
	}

	// partial wetness must never blur a more polished native reflection
	float GetFilmMipRoughness(float nativeRoughness, float wetness)
	{
		return min(FilmRoughness(wetness), nativeRoughness);
	}

	float3 GetFilmReflectionView(float3 normalView, float3 viewDir)
	{
		return normalView * -(2.0 * dot(viewDir, normalView)) + viewDir;
	}
}

#endif  // __WETNESS_EFFECTS_DEPENDENCY_HLSL__
