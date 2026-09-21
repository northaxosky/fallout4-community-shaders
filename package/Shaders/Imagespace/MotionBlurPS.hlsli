cbuffer MotionBlurParameters : register(b2)
{
	float4 Parameters[2];
};

Texture2D<float4> Scene : register(t0);
Texture2D<float4> Motion : register(t1);
SamplerState SceneSampler : register(s0);
SamplerState MotionSampler : register(s1);

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

float MotionWeight(float referenceLength, float2 coordinate)
{
	float2 velocity = Motion.Sample(MotionSampler, coordinate).xy;
	return saturate(
		1.0 - abs(referenceLength - length(velocity)) * Parameters[0].z);
}

float4 main(PS_INPUT input) : SV_Target
{
	float2 velocity = Motion.Sample(MotionSampler, input.TexCoord).xy;
	float velocityLength = length(velocity);
	float centerDistance = length(input.TexCoord - Parameters[1].zw);
	float blurLength =
		velocityLength * saturate(centerDistance * 0.1 - 0.018);
	if (blurLength < 0.001)
	{
		return Scene.Sample(SceneSampler, input.TexCoord);
	}

	float2 offset =
		normalize(velocity) * min(blurLength, Parameters[0].y) * Parameters[0].x;
	float2 centerCoordinate = min(input.TexCoord, Parameters[1].xy);
	float centerWeight = MotionWeight(velocityLength, centerCoordinate);
	float3 color = Scene.Sample(SceneSampler, centerCoordinate).xyz;
	float denominator = centerWeight;

	float4 tapCoordinates = input.TexCoord.xyxy +
		offset.xyxy * float4(0.25, 0.25, 0.5, 0.5);
	tapCoordinates = min(tapCoordinates, Parameters[1].xyxy);
	float firstWeight = MotionWeight(velocityLength, tapCoordinates.xy);
	denominator += firstWeight;
	color = mad(
		Scene.Sample(SceneSampler, tapCoordinates.xy).xyz,
		firstWeight,
		color * centerWeight);

	float secondWeight = MotionWeight(velocityLength, tapCoordinates.zw);
	denominator += secondWeight;
	color = mad(
		Scene.Sample(SceneSampler, tapCoordinates.zw).xyz,
		secondWeight,
		color);

	float2 finalCoordinate = min(
		input.TexCoord + offset * 0.75,
		Parameters[1].xy);
	float finalWeight = MotionWeight(velocityLength, finalCoordinate);
	denominator += finalWeight;
	color = mad(
		Scene.Sample(SceneSampler, finalCoordinate).xyz,
		finalWeight,
		color);

	return float4(color / (denominator + 0.001), 1.0);
}
