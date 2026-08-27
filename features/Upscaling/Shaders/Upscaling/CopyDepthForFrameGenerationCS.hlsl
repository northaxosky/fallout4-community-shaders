Texture2D<float> InputDepth : register(t0);
Texture2D<float2> InputMotion : register(t1);
Texture2D<float4> InputPreAlphaColor : register(t2);
Texture2D<float4> InputPostAlphaColor : register(t3);
RWTexture2D<float> OutputDepth : register(u0);
RWTexture2D<float2> OutputMotion : register(u1);

cbuffer CopyDimensions : register(b0)
{
	uint2 RenderSize;
	uint2 OutputSize;
	uint UseAlphaConditioning;
	uint3 Padding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint2 pixel = dispatchThreadID.xy;
	if (any(pixel >= OutputSize))
		return;

	uint2 inputSize;
	uint2 motionSize;
	InputDepth.GetDimensions(inputSize.x, inputSize.y);
	InputMotion.GetDimensions(motionSize.x, motionSize.y);
	const bool valid =
		all(pixel < RenderSize) && all(pixel < inputSize) && all(pixel < motionSize);
	const float depth = valid ? InputDepth.Load(int3(pixel, 0)) : 1.0;
	float2 motion = valid ? InputMotion.Load(int3(pixel, 0)) : 0.0;

	float mask = 1.0;
	if (valid && UseAlphaConditioning != 0) {
		const float3 preAlpha = InputPreAlphaColor.Load(int3(pixel, 0)).rgb;
		const float3 postAlpha = InputPostAlphaColor.Load(int3(pixel, 0)).rgb;
		const float3 difference = abs(preAlpha - postAlpha);
		mask = 1.0 - saturate(max(difference.x, max(difference.y, difference.z)) * 1000.0);
	}

	OutputDepth[pixel] = valid ? lerp(min(depth, 0.1), depth, mask) : 1.0;
	OutputMotion[pixel] = valid ? lerp(0.0, motion, mask) : 0.0;
}
