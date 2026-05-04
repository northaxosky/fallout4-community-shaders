Texture2D<float> DepthInput : register(t0);

RWTexture2D<float> DepthOutput : register(u0);

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= ScreenSize))
		return;

	// Point-sample from render resolution to screen resolution
	uint2 srcCoord = dispatchID.xy * RenderSize / ScreenSize;
	float depth = DepthInput[srcCoord];

	DepthOutput[dispatchID.xy] = depth;
}
