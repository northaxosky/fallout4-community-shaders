Texture2D<float4> InputTexturePreAlpha : register(t0);
Texture2D<float4> InputTextureAfterAlpha : register(t1);
Texture2D<float2> InputMotionVectors : register(t2);
Texture2D<float> InputDepth : register(t3);

RWTexture2D<float2> OutputMotionVectors : register(u0);
RWTexture2D<float> OutputDepth : register(u1);

[numthreads(8, 8, 1)] void main(uint3 DTid
								: SV_DispatchThreadID) {

	float3 colorPreAlpha  = InputTexturePreAlpha[DTid.xy].xyz;
	float3 colorPostAlpha = InputTextureAfterAlpha[DTid.xy].xyz;
	float depth = InputDepth[DTid.xy];

	float3 difference = abs(colorPreAlpha - colorPostAlpha);
	
	float mask = max(difference.x, max(difference.y, difference.z));
	mask *= 1000.0;
	mask = 1.0 - saturate(mask);
	
	OutputMotionVectors[DTid.xy] = lerp(0.0, InputMotionVectors[DTid.xy], mask);
	OutputDepth[DTid.xy] = lerp(min(depth, 0.1), depth, mask);
}
