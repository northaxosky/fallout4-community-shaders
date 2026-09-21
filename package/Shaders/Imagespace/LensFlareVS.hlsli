struct VS_INPUT
{
	float4 Position : POSITION;
	float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float4 TexCoord0 : TEXCOORD0;
	float3 TexCoord1 : TEXCOORD1;
};

cbuffer LensFlareParameters : register(b2)
{
	float4 Parameters[3];
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;
	float2 centered = Parameters[2].xy * 2.0 - 1.0;
	float2 position =
		centered * Parameters[2].w + input.Position.x * Parameters[1].xy;
	position += input.Position.y * Parameters[1].zw;
	output.Position.xy = position;
	output.TexCoord0.z = length(position) * 0.1;
	output.Position.zw = float2(0.0, 1.0);
	output.TexCoord0.xy = input.TexCoord;
	output.TexCoord0.w = 0.0;
	output.TexCoord1 = Parameters[2].xyz;
	return output;
}
