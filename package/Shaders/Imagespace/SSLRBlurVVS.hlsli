cbuffer BlurConstants : register(b0)
{
	float4 BlurDimensions;
};

struct VertexInput
{
	float4 position : POSITION;
	float2 texcoord : TEXCOORD0;
};

struct VertexOutput
{
	float4 position : SV_POSITION;
	float2 tap[5] : TEXCOORD0;
};

VertexOutput main(VertexInput input)
{
	VertexOutput output;
	output.position = float4(input.position.xyz, 1.0);

	float4 verticalOffsetCarrier = float4(
		-3.294215,
		rcp(BlurDimensions.y),
		-1.407333,
		1.407333);
	output.tap[0] = input.texcoord
		+ float2(0.0, -3.294215) * verticalOffsetCarrier.xy;
	output.tap[1] = input.texcoord
		+ float2(0.0, -1.407333) * verticalOffsetCarrier.zy;
	output.tap[2] = input.texcoord;
	output.tap[3] = input.texcoord
		+ float2(0.0, 1.407333) * verticalOffsetCarrier.wy;
	verticalOffsetCarrier.x = 3.294215;
	output.tap[4] = input.texcoord
		+ float2(0.0, 3.294215) * verticalOffsetCarrier.xy;
	return output;
}
