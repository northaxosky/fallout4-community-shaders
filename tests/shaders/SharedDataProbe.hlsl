#include "FO4/FO4ShaderData.hlsli"
#ifdef VOLUMETRIC_SHADOWS
#	include "FO4/VolumetricShadowsConsumer.hlsli"
#endif
#ifdef LIGHT_LIMIT_FIX
#	include "FO4/LightLimitFixRaster.hlsli"
#endif
#ifdef LIGHT_LIMIT_FIX_FORWARD
#	include "FO4/LightLimitFixForward.hlsli"
#endif
#ifdef LIGHT_LIMIT_FIX_STRICT
#	include "LightLimitFix/LightLimitFix.hlsli"
#endif
#ifdef FORWARD_DRAW_DESCRIPTOR
#	include "FO4/Permutation.hlsli"
#endif
#ifdef CLOUD_SHADOWS
#	include "FO4/WorldShadowConsumer.hlsli"
#endif
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
	value += FO4SharedData::DeltaTime;
	value += SharedData::DepthTexture.Load(int3(position.xy, 0)).x;
#	ifdef VOLUMETRIC_SHADOWS
	value += FO4VolumetricShadows::GetSoftShadow(position.xyz) + FO4VolumetricShadows::GetWaterSurfaceShadow(position.xyz, position.xy);
#	endif
#	ifdef LIGHT_LIMIT_FIX
	value += FO4LocalLightAttenuation(1.0, 0.5) + LightLimitFixLight.fade;
#	endif
#	ifdef LIGHT_LIMIT_FIX_FORWARD
	value += FO4LightLimitFix::GetConeAttenuation(0, 0.0, position.xyz);
#	endif
#	ifdef LIGHT_LIMIT_FIX_STRICT
	value += LightLimitFix::NumStrictLights + LightLimitFix::RoomIndex + LightLimitFix::ShadowBitMask + LightLimitFix::FirstPerson +
	         LightLimitFix::WorldEyePosition.x + LightLimitFix::StrictLights[0].fade;
#	endif
#	ifdef FORWARD_DRAW_DESCRIPTOR
	value += Permutation::ExtraShaderDescriptor + Permutation::EffectRadius;
#	endif
#	ifdef CLOUD_SHADOWS
	value += WorldShadow::GetMultFromViewPosition(position.xyz);
#	endif
#	ifdef ABI_SLOT_COLLISION
	value += CollisionValue;
#	endif
	return value.xxxx;
#else
	return 0.0;
#endif
}
