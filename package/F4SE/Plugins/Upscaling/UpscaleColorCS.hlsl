Texture2D<float4> ColorInput : register(t0);

RWTexture2D<float4> ColorOutput : register(u0);

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= ScreenSize))
		return;

	// Map screen pixel to source texel with manual bilinear interpolation
	float2 srcTexel = (float2(dispatchID.xy) + 0.5) * float2(RenderSize) / float2(ScreenSize) - 0.5;
	int2 t0 = max(int2(floor(srcTexel)), int2(0, 0));
	int2 t1 = min(t0 + int2(1, 1), int2(RenderSize) - 1);
	float2 f = srcTexel - floor(srcTexel);

	float4 s00 = ColorInput[int2(t0.x, t0.y)];
	float4 s10 = ColorInput[int2(t1.x, t0.y)];
	float4 s01 = ColorInput[int2(t0.x, t1.y)];
	float4 s11 = ColorInput[int2(t1.x, t1.y)];

	ColorOutput[dispatchID.xy] = lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
}
