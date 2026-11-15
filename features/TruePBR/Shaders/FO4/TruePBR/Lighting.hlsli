#ifndef FO4_TRUE_PBR_LIGHTING_HLSLI
#define FO4_TRUE_PBR_LIGHTING_HLSLI

#include "FO4/GBufferViews.hlsli"
#include "FO4/TruePBR/Adapter.hlsli"

namespace FO4TruePBR
{
	// The prepass class tag of a TruePBR pixel.
	static const float ClassTag = 128.0 / 255.0;

	struct Surface
	{
		bool active;
		float3 f0;
		float ao;
		float roughness;
	};

	bool IsPixel(float a_class)
	{
		return FO4SharedData::EnabledTruePBR != 0 && abs(a_class * 255.0 - 128.0) < 0.25;
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

	// Upstream's direct lobes on view-space vectors; every dot product is frame independent.
	// FO4: x pi, once, so the normalized lobes meet the light units legacy Lambert already uses.
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

	// Upstream's indirect diffuse weight divided by the base color (MRT0 is the post-metal base),
	// so it scales the ambient irradiance before the composite multiplies the base back in.
	float3 IndirectDiffuseThroughput(Surface a_surface, float3 a_baseColor, float3 a_normal, float3 a_viewDirection)
	{
		const float3 specularAlbedo = PBR::SpecularDirectionalAlbedo(a_surface.f0, a_surface.roughness, saturate(dot(a_normal, a_viewDirection)));
		return (1.0 - specularAlbedo) * MultiBounceAO(a_baseColor, a_surface.ao);
	}

	// Upstream's exported indirect albedo: the base color times the indirect diffuse weight.
	float3 ExportedAlbedo(float2 a_pixelPosition, float3 a_normal, float3 a_viewDirection, float3 a_baseColor)
	{
		const Surface surface = Decode(a_pixelPosition, FO4GBufferViews::Material.Load(int3(int2(a_pixelPosition), 0)));
		return surface.active ? a_baseColor * IndirectDiffuseThroughput(surface, a_baseColor, a_normal, a_viewDirection) : a_baseColor;
	}
}

#endif
