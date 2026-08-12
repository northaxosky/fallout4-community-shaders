Texture2D<float4> InputTexturePreAlpha : register(t0);
Texture2D<float4> InputTextureAfterAlpha : register(t1);
Texture2D<float2> InputMotionVectors : register(t2);
Texture2D<float> InputDepth : register(t3);

RWTexture2D<float2> OutputMotionVectors : register(u0);
RWTexture2D<float> OutputDepth : register(u1);
RWTexture2D<float> OutputUIAlpha : register(u2);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {

	float3 colorPreAlpha  = InputTexturePreAlpha[DTid.xy].xyz;
	float3 colorPostAlpha = InputTextureAfterAlpha[DTid.xy].xyz;
	float depth = InputDepth[DTid.xy];

	float3 difference = abs(colorPreAlpha - colorPostAlpha);
	float maxDiff = max(difference.x, max(difference.y, difference.z));

	float mask = 1.0 - saturate(maxDiff * 1000.0);

	OutputMotionVectors[DTid.xy] = lerp(0.0, InputMotionVectors[DTid.xy], mask);
	OutputDepth[DTid.xy] = lerp(min(depth, 0.1), depth, mask);

	// Reticle-pass pixels seed the UI alpha mask; UIAlphaMaskCS OR's the rest in at present-time.
	OutputUIAlpha[DTid.xy] = maxDiff > 0.5f / 255.0f ? 1.0f : 0.0f;
}
