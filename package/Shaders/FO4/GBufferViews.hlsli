#ifndef FO4_GBUFFER_VIEWS_HLSLI
#define FO4_GBUFFER_VIEWS_HLSLI

// FO4: host-bound, since these stages do not own the G-buffer views.
namespace FO4GBufferViews
{
	Texture2D<float4> Albedo : register(t69);
	Texture2D<float4> Normal : register(t70);
	Texture2D<float4> Material : register(t71);
	Texture2D<float4> Emissive : register(t72);

	// Prepass aux: R parallax shadow, B material AO.
	Texture2D<float4> Aux : register(t66);
	Texture2D<float4> F0 : register(t67);
}
#endif
