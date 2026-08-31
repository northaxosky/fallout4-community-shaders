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
	value += SharedData::screenSpaceGISettings.pad0;
	value += SharedData::screenSpaceGISettings.AoPower;
	value += SharedData::screenSpaceGISettings.BounceStrength;

	value += SharedData::wetnessEffectsSettings.Wetness;
	value += SharedData::wetnessEffectsSettings.MaxRainWetness;
	value += SharedData::wetnessEffectsSettings.MinRainWetness;
	value += SharedData::wetnessEffectsSettings.DebugVisualization;

	value += BoolValue(
		SharedData::terrainShadowsSettings.TerrainShadowMode != 0);
	value += dot(SharedData::terrainShadowsSettings.Scale, 1.0);
	value += dot(SharedData::terrainShadowsSettings.ZRange, 1.0);
	value += dot(SharedData::terrainShadowsSettings.Offset, 1.0);
	value += dot(SharedData::terrainShadowsSettings.HeightRange, 1.0);
	value += dot(SharedData::terrainShadowsSettings.DebugHeightRange, 1.0);

	value += SharedData::inverseSquareLightingSettings.Mode;
	value += SharedData::inverseSquareLightingSettings.ExteriorStrength;
	value += SharedData::inverseSquareLightingSettings.InteriorStrength;
	value += SharedData::inverseSquareLightingSettings.NearFieldDistance;

	value += SharedData::waterEffectsSettings.Mode;
	value += SharedData::waterEffectsSettings.HasWater;
	value += SharedData::waterEffectsSettings.WaterHeight;
	value += SharedData::waterEffectsSettings.pad0;

	value += SharedData::extendedTranslucencySettings.PackedMode;
	value += SharedData::extendedTranslucencySettings.AlphaReduction;
	value += SharedData::extendedTranslucencySettings.AlphaSoftness;
	value += SharedData::extendedTranslucencySettings.AlphaStrength;
	return value.xxxx;
#else
	return 0.0;
#endif
}
