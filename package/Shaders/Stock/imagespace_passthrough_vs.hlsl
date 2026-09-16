struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
// Unwritten by design: this axis widens the output signature and nothing else.
#ifdef IMAGESPACE_PASSTHROUGH_TEXCOORD1
	float4 TexCoord1 : TEXCOORD1;
#endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xyz, 1.0);
	vsout.TexCoord = input.TexCoord;
	return vsout;
}
