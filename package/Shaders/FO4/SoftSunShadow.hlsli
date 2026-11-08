#ifndef FO4_SOFT_SUN_SHADOW
#define FO4_SOFT_SUN_SHADOW

#include "FO4/SkylightingConsumer.hlsli"
#include "FO4/SunShadowFamilies.hlsli"

// Lobe source without Skylighting: the VSM, else the pass shadow.
#if defined(VOLUMETRIC_SHADOWS) && defined(FO4_SUN_SHADOWED_FAMILY)
#	include "FO4/VolumetricShadowsConsumer.hlsli"
#	define FO4_UNLOADED_SOFT_SHADOW
#	define FO4_UNLOADED_SOFT_SHADOW_BEGIN(softShadow, viewPosition) FO4_VSM_SOFT_SHADOW(softShadow, viewPosition)
#	define FO4_UNLOADED_SOFT_SHADOW_NATIVE(softShadow, shadow) FO4_VSM_NATIVE_SOFT_SHADOW(softShadow, shadow)
#elif defined(FO4_SUN_SHADOWED_FAMILY)
#	define FO4_UNLOADED_SOFT_SHADOW_BEGIN(softShadow, viewPosition)
#	define FO4_UNLOADED_SOFT_SHADOW_NATIVE(softShadow, shadow) softShadow = (shadow)
#else
#	define FO4_UNLOADED_SOFT_SHADOW_BEGIN(softShadow, viewPosition)
#	define FO4_UNLOADED_SOFT_SHADOW_NATIVE(softShadow, shadow)
#endif

// Soft, rim and back lobes take probe visibility (SHADOW_VIS), else the VSM.
#if defined(SKYLIGHTING_SHADOW_VIS)
#	define FO4_SOFT_SUN_SHADOW_ACTIVE
// FO4: Skylighting toggles live; disabled or non-opted lobes take the unloaded source.
#	define FO4_SKYLIGHTING_DISABLED [branch] if (!Skylighting::SoftSunVisibilityApplies())
#	define FO4_SOFT_SHADOW_BEGIN(softShadow, viewPosition, normalView, albedoSample, matSample) \
		Skylighting::SetSoftSunOptIn(albedoSample, matSample);                                   \
		FO4_SKYLIGHTING_SOFT_SHADOW(softShadow, viewPosition, normalView);                       \
		FO4_SKYLIGHTING_DISABLED { FO4_UNLOADED_SOFT_SHADOW_BEGIN(softShadow, viewPosition); }
#	define FO4_SOFT_SHADOW_NATIVE(softShadow, shadow) \
		FO4_SKYLIGHTING_DISABLED { FO4_UNLOADED_SOFT_SHADOW_NATIVE(softShadow, shadow); }
#elif defined(FO4_UNLOADED_SOFT_SHADOW)
#	define FO4_SOFT_SUN_SHADOW_ACTIVE
#	define FO4_SOFT_SHADOW_BEGIN(softShadow, viewPosition, normalView, albedoSample, matSample) \
		float softShadow;                                                                        \
		FO4_UNLOADED_SOFT_SHADOW_BEGIN(softShadow, viewPosition)
#	define FO4_SOFT_SHADOW_NATIVE(softShadow, shadow) FO4_UNLOADED_SOFT_SHADOW_NATIVE(softShadow, shadow)
#endif
#ifndef FO4_SOFT_SHADOW_NATIVE
#	define FO4_SOFT_SHADOW_NATIVE(softShadow, shadow)
#endif

#ifdef FO4_SOFT_SUN_SHADOW_ACTIVE
#	ifdef WATER_EFFECTS
#		define FO4_SOFT_SUN_CAUSTICS_TINT causticsMult
#	else
#		define FO4_SOFT_SUN_CAUSTICS_TINT 1.0
#	endif

namespace FO4SoftSunShadow
{
	// FO4: the pass scaled every lobe by shadow; this adds the soft shadow delta.
	float3 LobeDelta(float3 sunColor, float rim, float backfaceWrap, float3 albedoPremult, float forwardBlend, float3 albedo, float3 tint, float shadow, float softShadow)
	{
		float3 softDiffuse = sunColor * (rim + backfaceWrap * albedoPremult + forwardBlend * albedo) * tint;
		return softDiffuse * (softShadow - shadow);
	}
}

// Light-wide multipliers (terrain) scale the soft shadow too.
#	define FO4_SHADOW_LIGHT_MUL(shadow, softShadow, factor) \
		{                                                    \
			const float lightMul = (factor);                 \
			shadow *= lightMul;                              \
			softShadow *= lightMul;                          \
		}
#	define FO4_SOFT_SUN_LOBES(output, shadow, softShadow, sunColor, rim, backfaceWrap, albedoPremult, forwardBlend, albedo) \
		output.diffuse.xyz += FO4SoftSunShadow::LobeDelta(sunColor, rim, backfaceWrap, albedoPremult, forwardBlend, albedo, FO4_SOFT_SUN_CAUSTICS_TINT, shadow, softShadow) / 3.0
#else
#	define FO4_SOFT_SHADOW_BEGIN(softShadow, viewPosition, normalView, albedoSample, matSample)
#	define FO4_SHADOW_LIGHT_MUL(shadow, softShadow, factor) shadow *= (factor)
#	define FO4_SOFT_SUN_LOBES(output, shadow, softShadow, sunColor, rim, backfaceWrap, albedoPremult, forwardBlend, albedo)
#endif

// Unshadowed passes fold multipliers into the diffuse sum, so lobes stay apart.
#ifdef SKYLIGHTING_SHADOW_VIS
#	define FO4_SOFT_LOBES_DECLARE(softDiffuse) float3 softDiffuse = 0.0
#	define FO4_SOFT_LOBE(directDiffuse, softDiffuse) softDiffuse
#	define FO4_SOFT_LOBE_SCALE(softDiffuse, factor) softDiffuse *= (factor)
// Detailed-term factors reach the lobes only when Skylighting is off.
#	define FO4_SOFT_LOBE_SCALE_UNLOADED(softDiffuse, factor) \
		FO4_SKYLIGHTING_DISABLED { softDiffuse *= (factor); }
#	define FO4_SOFT_LOBES_APPLY(output, softDiffuse, softShadow) output.diffuse.xyz += (softDiffuse) * (softShadow) / 3.0
#else
#	define FO4_SOFT_LOBES_DECLARE(softDiffuse)
#	define FO4_SOFT_LOBE(directDiffuse, softDiffuse) directDiffuse
#	define FO4_SOFT_LOBE_SCALE(softDiffuse, factor)
#	define FO4_SOFT_LOBE_SCALE_UNLOADED(softDiffuse, factor)
#	define FO4_SOFT_LOBES_APPLY(output, softDiffuse, softShadow)
#endif
#endif
