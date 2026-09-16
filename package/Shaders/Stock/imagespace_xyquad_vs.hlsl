struct VS_INPUT
{
	float4 Position : POSITION0;
#ifdef IMAGESPACE_XYQUAD_PACKED
	float4 Color : COLOR0;
#endif
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
#ifdef IMAGESPACE_XYQUAD_PACKED
	float3 TexCoord : TEXCOORD0;
#endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xy, 0.0, 1.0);
#ifdef IMAGESPACE_XYQUAD_PACKED
	vsout.TexCoord = float3(input.Position.zw, 1.0);
#endif
	return vsout;
}
