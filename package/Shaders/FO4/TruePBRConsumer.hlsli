#ifndef FO4_TRUE_PBR_CONSUMER_HLSLI
#define FO4_TRUE_PBR_CONSUMER_HLSLI

// Lighting call sites stay declarative and compile away without the feature.
#ifdef FO4_TRUE_PBR
#	include "FO4/TruePBR/Lighting.hlsli"

#	ifdef SPECULAR
#		define FO4_TRUE_PBR_SPECULAR true
#	else
#		define FO4_TRUE_PBR_SPECULAR false
#	endif

// Only the diffuse and specular terms of a lit pixel are replaced.
#	define FO4_TRUE_PBR_DECODE(surface, pixelPosition, material) \
		FO4TruePBR::Surface surface = FO4TruePBR::Decode(pixelPosition, material)
#	define FO4_TRUE_PBR_AMBIENT(surface, albedo, normalView, viewDirection, ambient) \
		[branch] if (surface.active) ambient *= FO4TruePBR::IndirectDiffuseThroughput(surface, albedo, normalView, viewDirection)
// Upstream's skylighting takes the exported indirect albedo.
#	define FO4_TRUE_PBR_EXPORTED_ALBEDO(albedo, pixelPosition, normalView, viewPosition) \
		albedo = FO4TruePBR::ExportedAlbedo(pixelPosition, normalView, normalize(-(viewPosition)), albedo)
#	define FO4_TRUE_PBR_DIRECT(surface, normalView, viewDirection, lightDirection, lightColor, hasSpecular, diffuse, specular) \
		[branch] if (surface.active) FO4TruePBR::Direct(surface, normalView, viewDirection, lightDirection, lightColor, hasSpecular, diffuse, specular)
// Native terms the TruePBR lobes do not use: wet specular mixing, rim.
#	define FO4_TRUE_PBR_OVERRIDE(surface, variable, value) \
		[branch] if (surface.active) variable = value
// Composite stages; the includer adds FO4/TruePBR/Composite.hlsli.
#	define FO4_TRUE_PBR_RESOLVE(materialClass, result, litColor, ssao, pixelPosition, probeSampler) \
		[branch] if (FO4TruePBR::IsPixel(materialClass)) result = FO4TruePBR::Resolve(pixelPosition, litColor, ssao, probeSampler)
// A TruePBR pixel has no native reflection, also under a blended draw.
#	define FO4_TRUE_PBR_NO_NATIVE_REFLECTION(materialClass, reflectionScale) \
		[branch] if (FO4TruePBR::IsPixel(materialClass)) reflectionScale = 0.0
#else
#	define FO4_TRUE_PBR_SPECULAR false
#	define FO4_TRUE_PBR_DECODE(surface, pixelPosition, material)
#	define FO4_TRUE_PBR_AMBIENT(surface, albedo, normalView, viewDirection, ambient)
#	define FO4_TRUE_PBR_EXPORTED_ALBEDO(albedo, pixelPosition, normalView, viewPosition)
#	define FO4_TRUE_PBR_DIRECT(surface, normalView, viewDirection, lightDirection, lightColor, hasSpecular, diffuse, specular)
#	define FO4_TRUE_PBR_OVERRIDE(surface, variable, value)
#	define FO4_TRUE_PBR_RESOLVE(materialClass, result, litColor, ssao, pixelPosition, probeSampler)
#	define FO4_TRUE_PBR_NO_NATIVE_REFLECTION(materialClass, reflectionScale)
#endif
#endif
