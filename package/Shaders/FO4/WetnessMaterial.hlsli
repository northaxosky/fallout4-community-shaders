#ifndef FO4_WETNESS_MATERIAL_HLSLI
#define FO4_WETNESS_MATERIAL_HLSLI
#include "FO4/WetnessEffectsConsumer.hlsli"

namespace FO4Wetness
{
	float3 TessellatedDirectionToWorld(float3 projected)
	{
		// FO4: domain-stage basis lanes use the linear view-projection block.
		float3 a = FrameBuffer::CameraViewProj[0].xyz;
		float3 b = FrameBuffer::CameraViewProj[1].xyz;
		float3 c = FrameBuffer::CameraViewProj[2].xyz;
		return normalize((projected.x * cross(b, c) + projected.y * cross(c, a) + projected.z * cross(a, b)) / dot(a, cross(b, c)));
	}

	struct MaterialInput
	{
		float3 cameraRelativePosition;
		float3 modelPosition;
		float3 vertexNormal;
		float3 shadingNormal;
		float viewDepth;
		float environmentMask;
		bool environmentMapped;
		bool skinned;
		bool skinOrHair;
		bool excludeDarkening;
		bool inWorld;
	};

	float4 PrepareMaterial(MaterialInput input, inout float3 baseColor)
	{
		float waterHeight = SharedData::GetWaterData(input.cameraRelativePosition).w;
		float shoreFactor = saturate(1.0 - abs(input.cameraRelativePosition.z - waterHeight) / SharedData::wetnessEffectsSettings.ShoreRange);
		float shoreFactorAlbedo = input.cameraRelativePosition.z < waterHeight ? 1.0 : shoreFactor;
		float minWetnessAngle = saturate(max(SharedData::wetnessEffectsSettings.MinRainWetness, input.vertexNormal.z));
		float wetnessOcclusion = input.inWorld;
		float flatnessAmount = smoothstep(SharedData::wetnessEffectsSettings.PuddleMaxAngle, 1.0, minWetnessAngle);
		float4 raindropInfo = float4(0, 0, 1, 0);
		float3 absolutePosition = input.cameraRelativePosition + FrameBuffer::CameraPosAdjust.xyz;
		if (input.shadingNormal.z > 0.0 && SharedData::wetnessEffectsSettings.Raining > 0.0 &&
			SharedData::wetnessEffectsSettings.EnableRaindropFx && wetnessOcclusion > 0.5) {
			float3 ripplePosition = input.skinned ? input.modelPosition : absolutePosition;
			raindropInfo = WetnessEffects::GetRainDrops(ripplePosition, SharedData::wetnessEffectsSettings.Time, input.vertexNormal, flatnessAmount);
		}
		float rainWetness = SharedData::wetnessEffectsSettings.Wetness * minWetnessAngle * SharedData::wetnessEffectsSettings.MaxRainWetness;
		rainWetness = max(rainWetness, raindropInfo.w);
		if (input.skinOrHair)
			rainWetness = SharedData::wetnessEffectsSettings.SkinWetness * SharedData::wetnessEffectsSettings.Wetness;
		float wetness = max(shoreFactor * SharedData::wetnessEffectsSettings.MaxShoreWetness, rainWetness);
		float puddleWetness = SharedData::wetnessEffectsSettings.PuddleWetness * minWetnessAngle;
		float puddle = wetness;
		if (!input.skinned && (wetness > 0.0 || puddleWetness > 0.0)) {
			float3 puddleCoords = (absolutePosition * 0.5 + 0.5) * 0.01 / SharedData::wetnessEffectsSettings.PuddleRadius;
			puddle = Random::perlinNoise(puddleCoords) * 0.5 + 0.5;
			puddle = puddle * ((minWetnessAngle / SharedData::wetnessEffectsSettings.PuddleMaxAngle) * SharedData::wetnessEffectsSettings.MaxPuddleWetness * 0.25) + 0.5;
			puddle *= lerp(wetness, puddleWetness, saturate(puddle - 0.25));
		}
		puddle *= saturate(wetnessOcclusion * 2.0) * smoothstep(4096.0 * 2.5, 0.0, input.viewDepth);
		float3 wetnessNormal = lerp(input.shadingNormal, input.vertexNormal, saturate(puddle));
		float glossinessAlbedo = max(puddle, shoreFactorAlbedo * SharedData::wetnessEffectsSettings.MaxShoreWetness);
		glossinessAlbedo *= glossinessAlbedo;
		float glossinessSpecular = puddle;
		if (input.cameraRelativePosition.z < waterHeight)
			glossinessSpecular *= shoreFactor;
		flatnessAmount *= smoothstep(SharedData::wetnessEffectsSettings.PuddleMinWetness, 1.0, glossinessSpecular);
		float3 rippleNormal = normalize(lerp(float3(0, 0, 1), raindropInfo.xyz, lerp(flatnessAmount, 1.0, 0.5)));
		wetnessNormal = ReorientNormal(rippleNormal, wetnessNormal);
		float porosity = input.environmentMapped ? lerp(1.0, 0.0, saturate(sqrt(input.environmentMask))) : 1.0;
		if (!input.excludeDarkening)
			baseColor = lerp(baseColor, pow(abs(baseColor), 1.0 + porosity * glossinessAlbedo), 0.5);
		return float4(GBuffer::EncodeNormal(wetnessNormal), max(saturate(1.0 - glossinessSpecular), 0.05), 1.0);
	}
}
#endif
