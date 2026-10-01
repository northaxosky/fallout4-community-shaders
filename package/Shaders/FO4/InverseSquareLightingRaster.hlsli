#ifndef FO4_INVERSE_SQUARE_LIGHTING_RASTER_HLSLI
#define FO4_INVERSE_SQUARE_LIGHTING_RASTER_HLSLI

#ifdef INVERSE_SQUARE_LIGHTING
#	include "FO4/InverseSquareLightingConsumer.hlsli"
// FO4: native raster local lights publish per-draw metadata rather than LLF lists.
cbuffer InverseSquareDraw : register(b11)
{
	FO4InverseSquareLighting::PerLightData InverseSquareLight;
};
#endif

float3 FO4LocalLightColor(float3 nativeColor)
{
#if defined(INVERSE_SQUARE_LIGHTING) && !defined(DIRECTIONAL) && !defined(AMBIENT)
	return FO4InverseSquareLighting::GetColor(nativeColor, InverseSquareLight);
#else
	return nativeColor;
#endif
}
float FO4LocalLightAttenuation(float distance, float nativeAttenuation)
{
#ifdef INVERSE_SQUARE_LIGHTING
	return FO4InverseSquareLighting::GetAttenuation(InverseSquareLight, distance, nativeAttenuation);
#else
	return nativeAttenuation;
#endif
}
bool FO4LocalLightNegligible(float attenuation)
{
#ifdef INVERSE_SQUARE_LIGHTING
	if ((InverseSquareLight.lightFlags & LightLimitFix::LightFlags::InverseSquare) != 0)
		return attenuation < 1e-5;
#endif
	return attenuation <= 0.001;
}
#endif
