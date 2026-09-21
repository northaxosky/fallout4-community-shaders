struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

cbuffer FullScreenColorParameters : register(b2)
{
	float4 FullScreenColor;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	float3 color = lerp(Image.Sample(ImageSampler, input.TexCoord).rgb, FullScreenColor.rgb, FullScreenColor.a);
	return float4(color, 1.0);
}
