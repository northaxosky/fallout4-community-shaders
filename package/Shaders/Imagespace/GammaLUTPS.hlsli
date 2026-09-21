cbuffer GammaParameters : register(b2)
{
	float4 Parameters[2];
};

Texture2D<float4> Source : register(t0);
Texture3D<float4> Lut3 : register(t3);
Texture3D<float4> Lut4 : register(t4);
Texture3D<float4> Lut5 : register(t5);
Texture3D<float4> Lut6 : register(t6);
SamplerState SourceSampler : register(s0);
SamplerState Lut3Sampler : register(s3);
SamplerState Lut4Sampler : register(s4);
SamplerState Lut5Sampler : register(s5);
SamplerState Lut6Sampler : register(s6);

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
	float4 source = Source.Sample(SourceSampler, input.TexCoord);
	float3 coordinate = exp(log(source.rgb) * asfloat(0x3ee8ba2f));
	coordinate = mad(coordinate, 0.9375, 0.03125);
	float3 result =
		Lut3.Sample(Lut3Sampler, coordinate).xyz * Parameters[1].x;
	result = mad(
		Lut4.Sample(Lut4Sampler, coordinate).xyz,
		Parameters[1].y,
		result);
	result = mad(
		Lut5.Sample(Lut5Sampler, coordinate).xyz,
		Parameters[1].z,
		result);
	return float4(
		mad(Lut6.Sample(Lut6Sampler, coordinate).xyz, Parameters[1].w, result),
		source.w);
}
