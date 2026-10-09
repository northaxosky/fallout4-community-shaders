#ifndef FO4_FOG_CONSUMER
#define FO4_FOG_CONSUMER

// Fog sites compile away unless a fog feature consumes them.
#define FO4_FOG_VANILLA_COLOR(color) (color)
#ifdef EXPONENTIAL_HEIGHT_FOG
#	include "FO4/ExponentialHeightFogConsumer.hlsli"
// A vanilla fog tint joins here, ahead of the height fog composition.
#	define FO4_FOG_ORIGINAL(name, color) float3 name = color
#	define FO4_FOG_REPLACE(pixelPosition, originalColor, color, opacity) FO4Fog::Replace(pixelPosition, originalColor, color, opacity)
#	define FO4_FOG_REPLACE_FORWARD(screenPosition, originalColor, color, opacity) FO4Fog::ReplaceForward(screenPosition, originalColor, color, opacity)
#else
#	define FO4_FOG_ORIGINAL(name, color)
#	define FO4_FOG_REPLACE(pixelPosition, originalColor, color, opacity)
#	define FO4_FOG_REPLACE_FORWARD(screenPosition, originalColor, color, opacity)
#endif
#endif
