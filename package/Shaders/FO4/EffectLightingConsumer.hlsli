#ifndef FO4_EFFECT_LIGHTING_CONSUMER
#define FO4_EFFECT_LIGHTING_CONSUMER

// Without a scaling feature the split recombines to its input.
#if defined(LIGHTING) && (defined(SKYLIGHTING) || defined(TERRAIN_SHADOWS) || defined(VOLUMETRIC_SHADOWS))
#	define FO4_EFFECT_LIGHTING
#endif
// FO4: soft falloff volume; TEXTURE implies TEXCOORD.
#if !defined(LIGHTING) && defined(VC) && defined(NORMALS) && defined(TEXTURE) && defined(FALLOFF) && defined(SOFT) && defined(GRAYSCALE_TO_ALPHA) && defined(TERRAIN_SHADOWS)
#	define FO4_EFFECT_FALLOFF_SHADOW
#endif

#if defined(FO4_EFFECT_LIGHTING) || defined(FO4_EFFECT_FALLOFF_SHADOW)

#	include "FO4/FO4ShaderData.hlsli"
#	include "FO4/ForwardPosition.hlsli"
#	if defined(FO4_EFFECT_LIGHTING) && defined(SKYLIGHTING)
#		include "FO4/SkylightingConsumer.hlsli"
#	endif

#	include "FO4/EffectShadowSampling.hlsli"

namespace FO4EffectLighting
{
#	if defined(FO4_EFFECT_LIGHTING)
	// FO4: features toggle live; a disabled scaler skips its lookups.
	bool SunShadowEnabled()
	{
		bool enabled = false;
#		if defined(TERRAIN_SHADOWS)
		enabled = enabled || SharedData::terraOccSettings.EnableTerrainShadow;
#		endif
#		if defined(VOLUMETRIC_SHADOWS)
		enabled = enabled || FO4SharedData::EnabledVolumetricShadows != 0;
#		endif
		return enabled;
	}

	bool SkylightingEnabled()
	{
#		if defined(SKYLIGHTING)
		return FO4SharedData::EnabledSkylighting != 0;
#		else
		return false;
#		endif
	}

	float3 GetLightingColor(float3 dLightColor, float3 screenPosition)
	{
		// FO4: with every scaler off the split recombines to the unloaded input.
		[branch] if (!SkylightingEnabled() && !SunShadowEnabled()) return dLightColor;
		float3 viewPosition = FO4Forward::ViewPosition(screenPosition);
		float3 worldPosition = FrameBuffer::ViewToWorld(viewPosition);

		float3 color = dLightColor * Color::EffectLightingScale;
		bool suppressExternalEmittance = SharedData::InInterior && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::SuppressExternalEmittance);
		if (suppressExternalEmittance) {
			color = ShadowSampling::GetAmbientLighting() + ShadowSampling::GetDirectionalLighting();
		}

#		if defined(SKYLIGHTING)
		float skylightingDiffuse = 1.0;
		[branch] if (!SharedData::InInterior && SkylightingEnabled())
		{
			float3 positionMSSkylight = worldPosition;

			sh2 skylightingSH = Skylighting::SampleNoBias(positionMSSkylight);
			skylightingDiffuse = Skylighting::EvaluateDiffuse(skylightingSH, float3(0, 0, 1), Skylighting::GetFadeOutFactor(positionMSSkylight));
		}
#		endif

		float3 dirColor;
		float3 ambientColor;
		ShadowSampling::ExtractLighting(color, dirColor, ambientColor);

		float3 viewDirection = normalize(worldPosition.xyz);

		float unusedSurfaceShadow;
		float dirShadow = 1.0;

		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);

		[branch] if (inWorld && ShadowSampling::HasDirectionalShadows() && SunShadowEnabled())
			dirShadow = ShadowSampling::Get3DFilteredShadow(worldPosition.xyz, viewDirection, screenPosition.xy, unusedSurfaceShadow);

		dirColor *= dirShadow;

#		if defined(SKYLIGHTING)
#			if defined(IBL)
		if (!SharedData::iblSettings.EnableIBL)
#			endif
		{
			ambientColor = ColorManagement::SceneColor::ScaleByLinear(ambientColor, skylightingDiffuse);
		}
#		endif

		return dirColor + ambientColor;
	}
#	endif

#	if defined(FO4_EFFECT_FALLOFF_SHADOW)
	float3 GetLightingShadow(float3 color, float3 screenPosition, float depth)
	{
		// FO4: only terrain shadows this volume.
		[branch] if (!SharedData::terraOccSettings.EnableTerrainShadow) return color;
		float3 viewPosition = FO4Forward::ViewPosition(screenPosition);
		float3 worldPosition = FrameBuffer::ViewToWorld(viewPosition);

		float3 dirColor;
		float3 ambientColor;
		ShadowSampling::ExtractLighting(color, dirColor, ambientColor);

		static const uint sampleCount = 8;
		static const float rcpSampleCount = 1.0 / float(sampleCount);

		float noise = Random::InterleavedGradientNoise(screenPosition.xy, SharedData::FrameCount);
		float noiseTransform = noise * 2.0 - 1.0;
		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

		// Enough for sky statics
		float maxDistance = max(0, SharedData::GetScreenDepth(FO4Depth::ProjectionDepth(depth)));
		float viewRayLength = 2048.0;
		float3 viewDirection = normalize(worldPosition);
		float3 startPosition = worldPosition - viewDirection * viewRayLength;
		float3 endPosition = worldPosition + viewDirection * min(maxDistance, viewRayLength);

		float shadow = 1.0;

		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);

		if (inWorld && !SharedData::InInterior) {
			shadow = 0.0;
			for (uint i = 0; i < sampleCount; i++) {
				float t = (float(i) + noise) * rcpSampleCount;
				float3 samplePositionWS = lerp(startPosition, endPosition, t);
				shadow += ShadowSampling::GetWorldShadow(samplePositionWS, FrameBuffer::CameraPosAdjust.xyz);
			}
			shadow *= rcpSampleCount;
		}

		dirColor *= shadow;

		return dirColor + ambientColor;
	}
#	endif
}
#endif
#endif
