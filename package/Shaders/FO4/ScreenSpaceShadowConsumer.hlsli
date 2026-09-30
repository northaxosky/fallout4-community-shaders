#ifndef FO4_SCREEN_SPACE_SHADOW_CONSUMER
#define FO4_SCREEN_SPACE_SHADOW_CONSUMER

#include "FO4/Depth.hlsli"
#include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"

float2 FO4ScreenSpaceShadowVisibility(float3 position, float rawDepth)
{
	// FO4: pixel-centered SV_POSITION needs no extra half-pixel before integer mask lookup.
	position.xy -= 0.5;
	// FO4: first-person deferred receivers retain main's world-shadow exclusion.
	return !FO4Depth::IsFirstPerson(rawDepth) ? ScreenSpaceShadows::GetScreenSpaceShadows(position, 0.0.xx, 0.0) : 1.0.xx;
}

float FO4DirectionalScreenSpaceShadow(float3 position, float rawDepth, float lightAngle = 1.0)
{
	return lightAngle >= 0.0 ? FO4ScreenSpaceShadowVisibility(position, rawDepth).x : 1.0;
}

float FO4BackTransmissionScreenSpaceShadow(float3 position, float rawDepth, float lightAngle)
{
	// FO4: the native shared shadow term already applies front-facing visibility.
	return lightAngle < 0.0 ? FO4ScreenSpaceShadowVisibility(position, rawDepth).y : 1.0;
}
#endif
