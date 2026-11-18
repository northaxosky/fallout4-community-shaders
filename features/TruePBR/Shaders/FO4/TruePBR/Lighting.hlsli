#ifndef FO4_TRUE_PBR_LIGHTING_HLSLI
#define FO4_TRUE_PBR_LIGHTING_HLSLI

#include "FO4/GBufferViews.hlsli"
#include "FO4/TruePBR/Adapter.hlsli"
#include "FO4/TruePBR/Record.h"

namespace FO4TruePBR
{
	struct Surface
	{
		bool active;
		float3 f0;
		float ao;
		float roughness;
	};

	bool IsPixel(float a_class)
	{
		return FO4SharedData::EnabledTruePBR != 0 && TruePBRRecord::IsClassTag(a_class);
	}

	// a_material is MRT3: R is 1 - roughness, W the class tag.
	Surface Decode(float2 a_pixelPosition, float4 a_material)
	{
		Surface surface = (Surface)0;
		surface.active = IsPixel(a_material.w);
		[branch] if (surface.active)
		{
			const int3 texel = int3(int2(a_pixelPosition), 0);
			surface.f0 = FO4GBufferViews::F0.Load(texel).xyz;
			surface.ao = FO4GBufferViews::Aux.Load(texel).z;
			surface.roughness = clamp(1.0 - a_material.x, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
		}
		return surface;
	}

	// FO4: x pi matches the light units legacy Lambert uses.
	void Direct(Surface a_surface, float3 a_normal, float3 a_viewDirection, float3 a_lightDirection, float3 a_lightColor, bool a_specular, out float3 a_diffuse, out float3 a_specularOut)
	{
		DirectContext context = (DirectContext)0;
		context.worldNormal = a_normal;
		context.vertexNormal = a_normal;
		context.viewDir = a_viewDirection;
		context.lightDir = a_lightDirection;
		context.halfVector = normalize(a_viewDirection + a_lightDirection);
		context.lightColor = a_lightColor;
		context.detailedShadow = 1.0;
		context.softShadow = 1.0;
		context.backTransmissionVisibility = 1.0;

		MaterialProperties material = (MaterialProperties)0;
		material.Roughness = a_surface.roughness;
		material.F0 = a_surface.f0;
		material.AO = a_surface.ao;

		DirectLightingOutput lighting;
		PBR::GetDirectLightInput(lighting, context, material, float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1), 0.0);
		a_diffuse = lighting.diffuse * Math::PI;
		a_specularOut = a_specular ? lighting.specular * Math::PI : 0.0;
	}

	IndirectLobeWeights IndirectLobes(Surface a_surface, float3 a_baseColor, float3 a_normal, float3 a_viewDirection)
	{
		IndirectContext context = (IndirectContext)0;
		context.worldNormal = a_normal;
		context.vertexNormal = a_normal;
		context.viewDir = a_viewDirection;

		MaterialProperties material = (MaterialProperties)0;
		material.BaseColor = a_baseColor;
		material.Roughness = a_surface.roughness;
		material.F0 = a_surface.f0;
		material.AO = a_surface.ao;

		IndirectLobeWeights weights;
		PBR::GetIndirectLobeWeights(weights, context, material);
		return weights;
	}

	// FO4: the composite multiplies the base color back in.
	float3 IndirectDiffuseThroughput(Surface a_surface, float3 a_baseColor, float3 a_normal, float3 a_viewDirection)
	{
		return IndirectLobes(a_surface, a_baseColor, a_normal, a_viewDirection).diffuse / max(a_baseColor, 1e-6);
	}

	// Skylighting and SSGI take upstream's indirect diffuse weight.
	float3 ExportedAlbedo(float2 a_pixelPosition, float3 a_normal, float3 a_viewDirection, float3 a_baseColor)
	{
		const Surface surface = Decode(a_pixelPosition, FO4GBufferViews::Material.Load(int3(int2(a_pixelPosition), 0)));
		return surface.active ? IndirectLobes(surface, a_baseColor, a_normal, a_viewDirection).diffuse : a_baseColor;
	}
}

#endif
