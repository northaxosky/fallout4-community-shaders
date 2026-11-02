// Engine alpha-tested Z-prepass PS; the stock one has no mip bias.
#include "FO4/FO4ShaderData.hlsli"

Texture2D<float4> BaseTex : register(t0);
SamplerState BaseSampler : register(s0);

cbuffer PerMaterial : register(b2)
{
	float4 AlphaData;
};

void main(float4 position : SV_POSITION, float3 uv : TEXCOORD0)
{
	float alpha = SAMPLE_MATERIAL(BaseTex, BaseSampler, uv.xy).w;
	clip(alpha * saturate(uv.z + AlphaData.y) - AlphaData.x);
}
