#include "FO4/ScreenSpaceShadowConsumer.hlsli"

Texture2D<float> PartitionedDepth : register(t0);
RWTexture2D<float> Visibility : register(u0);

[numthreads(8, 1, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	uint2 size;
	Visibility.GetDimensions(size.x, size.y);
	if (any(pixel >= size))
		return;
	float3 position = float3(float2(pixel) + 0.5, 0.0);
	float depth = PartitionedDepth.Load(int3(pixel, 0));
#ifdef SSS_BACK_TRANSMISSION
	Visibility[pixel] = FO4BackTransmissionScreenSpaceShadow(position, depth, -1.0);
#else
	Visibility[pixel] = FO4DirectionalScreenSpaceShadow(position, depth, 1.0);
#endif
}
