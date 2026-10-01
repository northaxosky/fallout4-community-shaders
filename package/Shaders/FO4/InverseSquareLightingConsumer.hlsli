#ifndef FO4_INVERSE_SQUARE_LIGHTING_CONSUMER_HLSLI
#define FO4_INVERSE_SQUARE_LIGHTING_CONSUMER_HLSLI

namespace LightLimitFix
{
#include "LightLimitFix/Common.hlsli"
}
#include "Common/Color.hlsli"
#include "InverseSquareLighting/InverseSquareLighting.hlsli"

namespace FO4InverseSquareLighting
{
	struct PerLightData
	{
		float3 color;
		float fade;
		float radius;
		float invRadius;
		float fadeZone;
		float sizeBias;
		uint lightFlags;
		uint3 pad0;
	};

	float3 GetColor(float3 nativeColor, PerLightData data)
	{
		if ((data.lightFlags & LightLimitFix::LightFlags::Disabled) != 0)
			return 0.0;
		if ((data.lightFlags & LightLimitFix::LightFlags::InverseSquare) == 0)
			return nativeColor;
		return Color::PointLight(data.color,
				   (data.lightFlags & LightLimitFix::LightFlags::Linear) != 0) *
		       data.fade;
	}

	float GetAttenuation(PerLightData data, float distance, float nativeAttenuation)
	{
		if ((data.lightFlags & LightLimitFix::LightFlags::Disabled) != 0)
			return 0.0;
		// FO4: unflagged lights retain the native radial curve, not LLF's regular curve.
		if ((data.lightFlags & LightLimitFix::LightFlags::InverseSquare) == 0)
			return nativeAttenuation;
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		light.radius = data.radius;
		light.invRadius = data.invRadius;
		light.fadeZone = data.fadeZone;
		light.sizeBias = data.sizeBias;
		light.lightFlags = data.lightFlags;
		return InverseSquareLighting::GetAttenuation(distance, light);
	}
}
#endif
