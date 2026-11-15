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

// The TruePBR branch replaces only the diffuse and specular terms of a lit pixel.
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
#else
#	define FO4_TRUE_PBR_SPECULAR false
#	define FO4_TRUE_PBR_DECODE(surface, pixelPosition, material)
#	define FO4_TRUE_PBR_AMBIENT(surface, albedo, normalView, viewDirection, ambient)
#	define FO4_TRUE_PBR_EXPORTED_ALBEDO(albedo, pixelPosition, normalView, viewPosition)
#	define FO4_TRUE_PBR_DIRECT(surface, normalView, viewDirection, lightDirection, lightColor, hasSpecular, diffuse, specular)
#	define FO4_TRUE_PBR_OVERRIDE(surface, variable, value)
#endif
#endif
