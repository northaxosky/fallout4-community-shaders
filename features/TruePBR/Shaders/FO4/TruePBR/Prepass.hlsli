#ifndef FO4_TRUE_PBR_PREPASS_HLSLI
#define FO4_TRUE_PBR_PREPASS_HLSLI

#include "FO4/FO4ShaderData.hlsli"
#include "FO4/TruePBR/Record.h"

// FO4: PBRMath reads this in thin-surface code the prepass never calls.
static const uint PBRFlags = 0;
#include "Common/PBRMath.hlsli"

namespace TruePBRPrepass
{
	StructuredBuffer<TruePBRRecord::Row> Records : register(t68);

	static const float ClassTag = 128.0 / 255.0;

	struct Surface
	{
		float3 baseColor;  // after metal, as upstream material.BaseColor
		float3 f0;
		float roughness;
		float metallic;
		float ao;
	};

	// FO4: the vertex shader outputs pow(color, 2.2); this inverts it per pixel.
	float3 EncodedVertexColor(float3 a_vertexColor)
	{
		return pow(a_vertexColor, 1.0 / 2.2);
	}

	// Upstream's vertex color weight; base color and emission differ only in epsilon.
	float3 VertexColorWeight(float3 a_encoded, float a_epsilon)
	{
		float3 color = ColorManagement::PBRVertexColorToLinear(a_encoded);
		const float vertexAO = max(max(color.x, color.y), color.z);
		color = vertexAO == 0.0 ? float3(1.0, 1.0, 1.0) : color * lerp(1 / max(vertexAO, a_epsilon), 1, SharedData::truePBRSettings.VertexAOStrength);
		return Color::LinearSRGBToWorking(color);
	}

	// Lighting.hlsl:1533,2043-2082; FO4: the sRGB view already decodes the base color.
	Surface Decode(float3 a_baseSample, float4 a_rmaosSample, TruePBRRecord::Row a_row, float3 a_encodedVertexColor)
	{
		const float4 rmaos = a_rmaosSample * float4(a_row.roughnessScale, 1, 1, a_row.specularLevel);
		Surface surface;
		surface.roughness = clamp(rmaos.x, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
		surface.metallic = saturate(rmaos.y);
		surface.ao = rmaos.z;
		const float3 base = Color::PBRMaterialToLinear(a_baseSample) * VertexColorWeight(a_encodedVertexColor, 0.001);
		surface.f0 = lerp(rmaos.www, base, surface.metallic.xxx);
		surface.baseColor = Color::LinearToPBRMaterial(base) * (1.0 - surface.metallic);
		return surface;
	}

	// FO4 translation of Lighting.hlsl:2823-2843: the engine constant is pow(color * scale, 2.2),
	// so the authored color and scale are recovered from it and the lane's live scale.
	float3 Emission(float3 a_glow, float3 a_engineConstant, float a_scale, float3 a_encodedVertexColor)
	{
		if (a_scale <= 0.0)
			return 0.0;
		const float3 emitColor = ColorManagement::SRGBToWorking(pow(max(a_engineConstant, 0.0), 1.0 / 2.2) / a_scale) * a_scale;
		const float3 glowColor = ColorManagement::TextureToWorking(a_glow, true) * SharedData::linearLightingSettings.glowmapMult;
		return emitColor * Color::LinearToPBRMaterial(glowColor) * Color::LinearToPBRMaterial(VertexColorWeight(a_encodedVertexColor, 1e-4));
	}

	// Lighting.hlsl:2921 on the encoded vertex color, stored as 1 - vertexAO.
	float VertexOcclusion(float3 a_encodedVertexColor)
	{
		const float vertexAO = ColorManagement::SRGBToWorking(max(max(a_encodedVertexColor.x, a_encodedVertexColor.y), a_encodedVertexColor.z));
		return 1.0 - lerp(1, vertexAO, SharedData::truePBRSettings.VertexAOStrength);
	}

	// Spec 5.2: the native wet gloss response, relative to dry, keeps the authored roughness at g = 0.
	float WetRoughness(float a_dryRoughness, float a_gloss, float a_dryGloss)
	{
		return a_dryGloss > 0.0 ? clamp(1.0 - (1.0 - a_dryRoughness) * a_gloss / a_dryGloss, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness) : a_dryRoughness;
	}

	// Lighting.hlsl:2948-2965 with the native wetness as the wet-albedo signal.
	float3 WetDarkening(float3 a_baseColor, float a_metallic, float a_wetness)
	{
		const float porosity = 1.0 - saturate(sqrt(a_metallic));
		return lerp(a_baseColor, pow(abs(a_baseColor), 1.0 + porosity * a_wetness * a_wetness), 0.5);
	}
}
#endif
