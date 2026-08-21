#include "Common/SharedData.hlsli"

float BoolValue(bool value)
{
	return value ? 1.0 : 0.0;
}

float4 main() : SV_Target
{
#ifdef FO4CS_SUBSTRATE
	float value = 0.0;
	value += dot(SharedData::CameraData, 1.0);
	value += dot(SharedData::BufferDim, 1.0);
	value += dot(SharedData::DynamicResolution, 1.0);
	value += dot(SharedData::NDCToViewMul, 1.0);
	value += dot(SharedData::NDCToViewAdd, 1.0);
	value += dot(SharedData::SunDirection, 1.0);
	value += SharedData::Timer + SharedData::DeltaTime;
	value += SharedData::FrameCount;
	value += BoolValue(SharedData::InInterior);

	value += BoolValue(
		SharedData::screenSpaceShadowsSettings.EnableScreenSpaceShadows);
	value += SharedData::screenSpaceShadowsSettings.ShadowContrast;
	value += dot(
		float2(SharedData::screenSpaceShadowsSettings.pad0),
		1.0);

	value += BoolValue(
		SharedData::screenSpaceGISettings.EnableScreenSpaceGI);
	value += BoolValue(
		SharedData::screenSpaceGISettings.EnableAmbientPass);
	value += SharedData::screenSpaceGISettings.AoPower;
	value += SharedData::screenSpaceGISettings.BounceStrength;

	value += BoolValue(
		SharedData::wetnessEffectsSettings.EnableWetnessEffects);
	value += BoolValue(
		SharedData::wetnessEffectsSettings.EnableAmbientPass);
	value += SharedData::wetnessEffectsSettings.Wetness;
	value += SharedData::wetnessEffectsSettings.pad0;
	return value.xxxx;
#else
	return 0.0;
#endif
}
