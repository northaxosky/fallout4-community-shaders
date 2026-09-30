#ifdef VSHADER
float4 main(uint vertex : SV_VertexID) : SV_Position
{
	float2 uv = float2((vertex << 1) & 2, vertex & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
#else
#	include "FO4/FO4ShaderData.hlsli"
// FO4: an isolated pass preserves composite's native s14.
SamplerState SampColorSampler : register(s14);
#	include "WaterEffects/WaterCaustics.hlsli"

float4 main(float4 screenPosition : SV_Position) : SV_Target
{
	float2 uv = screenPosition.xy * SharedData::BufferDim.zw;
	float depth = SharedData::DepthTexture.Load(int3(uint2(screenPosition.xy), 0)).x;
	float2 renderUV = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(uv);
	float4 position = mul(FrameBuffer::CameraViewProjInverse,
		float4(renderUV * float2(2, -2) + float2(-1, 1), depth, 1));
	position.xyz /= position.w;
	float4 water = SharedData::GetWaterData(position.xyz);
	float distance = water.w - position.z;
	float3 color = 0.0;
	if (distance > 0.0 && depth < 1.0) {
#	ifdef WATER_SUBMERSION_DEBUG
		color = saturate(distance / 1024.0).xxx;
#	else
		color = saturate(WaterEffects::ComputeCaustics(water, position.xyz) * 0.25);
#	endif
	}
	return float4(color, 1);
}
#endif
