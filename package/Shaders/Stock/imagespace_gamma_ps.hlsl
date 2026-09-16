// Imagespace gamma-curve pixel transform: one sampler-bound tap of one view, an optional constant coordinate scale, then one power curve over the three color lanes.
// Axes UV_PRE_TRANSFORM identity or constant_scale and CURVE_EXPONENT as a measured decimal literal; names are authored because the containers ship no reflection chunk.

// An absent axis define would otherwise read as zero and silently drop the coordinate scale or flatten the curve.
#if !defined(IMAGESPACE_GAMMA_UV_PRE_TRANSFORM) || IMAGESPACE_GAMMA_UV_PRE_TRANSFORM < 0 || IMAGESPACE_GAMMA_UV_PRE_TRANSFORM > 1 || !defined(IMAGESPACE_GAMMA_CURVE_EXPONENT)
#error "define IMAGESPACE_GAMMA_UV_PRE_TRANSFORM as zero or one and IMAGESPACE_GAMMA_CURVE_EXPONENT as the measured decimal curve literal"
#endif

#if IMAGESPACE_GAMMA_UV_PRE_TRANSFORM
cbuffer CoordinateParameters : register(b2)
{
	float4 CoordinateScale; // Scale in .xy; .zw are unread by every measured cell and only fill the single declared vector.
};
#endif

Texture2D<float4> Source : register(t0);
SamplerState SourceSampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 texCoord : TEXCOORD0) : SV_Target
{
#if IMAGESPACE_GAMMA_UV_PRE_TRANSFORM
	texCoord *= CoordinateScale.xy;
#endif
	float4 tap = Source.Sample(SourceSampler, texCoord);
	// The curve covers the three color lanes only; the fetched alpha reaches the output as a plain move.
	return float4(pow(tap.rgb, IMAGESPACE_GAMMA_CURVE_EXPONENT), tap.a);
}
