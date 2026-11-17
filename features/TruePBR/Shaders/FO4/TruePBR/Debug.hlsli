#ifndef FO4_TRUE_PBR_DEBUG_HLSLI
#define FO4_TRUE_PBR_DEBUG_HLSLI

#include "FO4/FO4SharedData.hlsli"

namespace TruePBRDebug
{
	bool IsOwner()
	{
		return FO4SharedData::DebugOwner == FullscreenDebugOwner::TruePBR;
	}

	// Shown on any tagged draw, not only on PBR pixels.
	bool IsRecordView()
	{
		return IsOwner() && (FO4SharedData::DebugMode == TruePBRDebugMode::Classified || FO4SharedData::DebugMode == TruePBRDebugMode::Record);
	}

	bool IsMaterialView()
	{
		return IsOwner() && FO4SharedData::DebugMode >= TruePBRDebugMode::BaseColor && FO4SharedData::DebugMode <= TruePBRDebugMode::AmbientOcclusion;
	}

	bool IsParallaxView()
	{
		return IsOwner() && FO4SharedData::DebugMode == TruePBRDebugMode::ParallaxOffset;
	}

	// Golden-ratio hue steps keep consecutive ids visibly apart.
	float3 RecordColor(uint a_record)
	{
		const float hue = frac(float(a_record) * 0.61803398875);
		return saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
	}

	// The flat color rides the emissive output over zero albedo.
	void Apply(uint a_record, inout float3 a_albedo, inout float3 a_emissive)
	{
		a_albedo = 0.0;
		a_emissive = FO4SharedData::DebugMode == TruePBRDebugMode::Record ? RecordColor(a_record) : float3(1.0, 0.0, 1.0);
	}

	void ApplyParallax(float2 a_uvDelta, float a_shadow, inout float3 a_albedo, inout float3 a_emissive)
	{
		a_albedo = 0.0;
		a_emissive = float3(abs(a_uvDelta) * 10.0, a_shadow);
	}

	void ApplyMaterial(float3 a_baseColor, float a_roughness, float3 a_f0, float a_ao, inout float3 a_albedo, inout float3 a_emissive)
	{
		a_albedo = 0.0;
		const uint mode = FO4SharedData::DebugMode;
		a_emissive = mode == TruePBRDebugMode::BaseColor  ? a_baseColor :
		             mode == TruePBRDebugMode::Roughness  ? float3(a_roughness, a_roughness, a_roughness) :
		             mode == TruePBRDebugMode::MetallicF0 ? a_f0 :
		                                                    float3(a_ao, a_ao, a_ao);
	}
}
#endif
