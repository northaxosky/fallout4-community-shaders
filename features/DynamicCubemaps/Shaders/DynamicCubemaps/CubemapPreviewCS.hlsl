TextureCube<float3> InputCubemap : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);
SamplerState LinearSampler : register(s0);

static const float Pi = 3.14159265358979323846;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint width;
	uint height;
	OutputTexture.GetDimensions(width, height);
	if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
		return;

	float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(width, height);
	float longitude = (uv.x * 2.0 - 1.0) * Pi;
	float latitude = (0.5 - uv.y) * Pi;
	float cosLatitude = cos(latitude);
	float3 direction = float3(
		cosLatitude * cos(longitude),
		cosLatitude * sin(longitude),
		sin(latitude));
	float3 color = max(InputCubemap.SampleLevel(
		LinearSampler, direction, 0.0), 0.0);
	color = color / (1.0 + color);
	color = pow(color, 1.0 / 1.6);
	OutputTexture[dispatchThreadId.xy] = float4(color, 1.0);
}
