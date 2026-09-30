#include "FO4/FO4ShaderData.hlsli"
#ifdef ABI_SLOT_COLLISION
cbuffer Collision : register(ABI_SLOT_COLLISION)
{
	float CollisionValue;
};
#endif

float4 main(float4 position : SV_Position) : SV_Target
{
#ifdef FO4CS_SUBSTRATE
	float value = FrameBuffer::CameraViewProj[0][0] + FrameBuffer::DynamicResolutionParams2.w;
	value += SharedData::WaterData[24].w + SharedData::HDRData.w;
	value += SharedData::grassLightingSettings.Glossiness + SharedData::horizonFixSettings.farWaterDistance;
	value += FO4SharedData::screenSpaceShadowsSettings.ShadowContrast + FO4SharedData::DeltaTime;
	value += SharedData::DepthTexture.Load(int3(position.xy, 0)).x;
#	ifdef ABI_SLOT_COLLISION
	value += CollisionValue;
#	endif
	return value.xxxx;
#else
	return 0.0;
#endif
}
