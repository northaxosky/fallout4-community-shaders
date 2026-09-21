struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	return Image.Sample(ImageSampler, input.TexCoord);
}
