struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float4 TexCoord0 : TEXCOORD0;
	float3 TexCoord1 : TEXCOORD1;
};

cbuffer LensFlareParameters : register(b2)
{
	float4 Parameters[3];
};

Texture2D<float4> Image : register(t0);
Texture2D<float4> Values : register(t1);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target
{
	int valueIndex = (int)Parameters[1].x;
	float4 values = Values.Load(int3(valueIndex, 0, 0));
	float value = dot(Parameters[2], values);
	float2 warped = (input.TexCoord0.xy - 0.5) / (value + 0.001) + 0.5;
	float2 texCoord =
		Parameters[0].w > 0.0 ? warped : input.TexCoord0.xy;
	float3 color = Image.Sample(ImageSampler, texCoord).rgb;
	return float4(value * Parameters[0].xyz * color, 0.0);
}
