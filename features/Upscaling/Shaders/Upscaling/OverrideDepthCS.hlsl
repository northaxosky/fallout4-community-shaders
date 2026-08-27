Texture2D<float> DepthInput : register(t0);

RWTexture2D<float> DepthOutput : register(u0);

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside texture dimensions
	if (any(dispatchID.xy >= RenderSize))
		return;

	float depth = DepthInput[dispatchID.xy];

	DepthOutput[dispatchID.xy] = depth;
}