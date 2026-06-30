Texture2D<float2> MotionVectorInput : register(t0);
Texture2D<float> DepthInput : register(t1);

RWTexture2D<float2> MotionVectorOutput : register(u0);

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;
};

float GetScreenDepth(float depth)
{
	return (CameraData.w / (-depth * CameraData.z + CameraData.x));
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= RenderSize))
		return;

	float depth = DepthInput[dispatchID.xy];

	// Dilate using the longest closer motion vector in a 5x5 neighborhood.
	float2 motionVector = MotionVectorInput[dispatchID.xy];
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	if (GetScreenDepth(depth) > (4096 * 2.5)){
		[unroll]
		for (int y = -2; y <= 2; y++) {
			[unroll]
			for (int x = -2; x <= 2; x++) {
				int2 samplePos = int2(dispatchID.xy) + int2(x, y);

				if (any(samplePos < 0) || any(samplePos >= int2(RenderSize)))
					continue;

				float neighborDepth = DepthInput[samplePos];

				if (neighborDepth < depth){
					float2 neighborMotionVector = MotionVectorInput[samplePos];

					float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

					if (motionLengthSq > maxMotionLengthSq){
						maxMotionLengthSq = motionLengthSq;
						longestMotionVector = neighborMotionVector;
					}
				}
			}
		}
	}

	MotionVectorOutput[dispatchID.xy] = longestMotionVector;
}