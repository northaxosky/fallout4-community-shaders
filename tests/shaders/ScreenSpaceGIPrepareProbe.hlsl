#include "ScreenSpaceGI/common.hlsli"

Texture2D<float3> Reference : register(t0);
Texture2D<float3> Prepared : register(t1);
RWTexture2D<float4> Difference : register(u0);

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID)
{
	// Match radianceDisocc, including its unguarded dispatch padding and physical texture clamp.
	float2 frameScale = FrameDim * RcpTexDim;
	float2 uv = (pixel + .5) * RCP_OUT_FRAME_DIM;
	float3 reference = FULLRES_LOAD(Reference, pixel, uv * frameScale, samplerLinearClamp);
	float3 prepared = FULLRES_LOAD(Prepared, pixel, uv * frameScale, samplerLinearClamp);
	Difference[pixel] = float4(abs(reference - prepared), 0);
}
