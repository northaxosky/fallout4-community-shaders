#ifndef FO4_GBUFFER_VIEWS_HLSLI
#define FO4_GBUFFER_VIEWS_HLSLI

// FO4: G-buffer views the host binds for the light, tiled and composite stages.
namespace FO4GBufferViews
{
	Texture2D<float4> Albedo : register(t69);
	Texture2D<float4> Normal : register(t70);
	Texture2D<float4> Material : register(t71);
	Texture2D<float4> Emissive : register(t72);

	// Published by the prepass for every stage that reads it: R parallax shadow, B material AO.
	Texture2D<float4> Aux : register(t66);
	// TruePBR reflectance at normal incidence.
	Texture2D<float4> F0 : register(t67);
}
#endif
