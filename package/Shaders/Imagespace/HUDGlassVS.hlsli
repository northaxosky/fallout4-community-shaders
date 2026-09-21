#if defined(IMAGESPACE_HUD_GLASS_CLEAR)

struct VS_INPUT
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
};

float4 main(VS_INPUT input) : SV_POSITION
{
	return float4(input.Position.xyz, 1.0);
}

#else

struct VS_INPUT
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float3 TexCoord : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;
	output.Position = float4(input.Position.xyz, 1.0);
	output.TexCoord.xy = input.Position.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
	output.TexCoord.z = input.Color.w;
	return output;
}

#endif
