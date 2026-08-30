// SPDX-License-Identifier: GPL-3.0-only
#include "Common/SharedData.hlsli"

namespace WaterEffects
{
	Texture2D<float4> WaterCaustics : register(t32);
	Texture2D<float> SceneDepthTexture : register(t33);

	static const uint MODE_DISABLED = 0;
	static const uint MODE_NORMAL = 1;
	static const uint MODE_CAUSTICS = 2;
	static const uint MODE_SUBMERSION = 3;

	static const float SHORE_RANGE = 64.0;
	static const float FADE_RANGE = 1024.0;
	static const float DISPERSION_RANGE = 256.0;
	static const float UV_SCALE = 0.005;
	static const float CAUSTICS_GAIN = 4.0;

	float2 PanCausticsUV(float2 uv, float speed, float tiling)
	{
		return frac((float2(1, 0) * SharedData::Timer * speed) + (uv * tiling));
	}

#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
	// The composite has no free sampler slot, so fetch manually. Safe because
	// PanCausticsUV already wraps its own UVs.
	float SampleCaustics(float2 uv)
	{
		uint2 dims;
		WaterCaustics.GetDimensions(dims.x, dims.y);
		if (any(dims == 0))
			return 1.0;
		int2 extent = int2(dims);
		float2 texel = uv * float2(dims) - 0.5;
		float2 base = floor(texel);
		float2 weight = texel - base;
		int2 lo = int2(base);
		int2 hi = lo + 1;
		int2 wlo = ((lo % extent) + extent) % extent;
		int2 whi = ((hi % extent) + extent) % extent;
		float s00 = WaterCaustics.Load(int3(wlo.x, wlo.y, 0)).x;
		float s10 = WaterCaustics.Load(int3(whi.x, wlo.y, 0)).x;
		float s01 = WaterCaustics.Load(int3(wlo.x, whi.y, 0)).x;
		float s11 = WaterCaustics.Load(int3(whi.x, whi.y, 0)).x;
		return lerp(
			lerp(s00, s10, weight.x), lerp(s01, s11, weight.x), weight.y);
	}
#else
	// WaterCausticsSampler comes from WaterCausticsSampler.hlsli, which only
	// the light path includes.
	float SampleCaustics(float2 uv)
	{
		return WaterCaustics.Sample(WaterCausticsSampler, uv).x;
	}
#endif

	float3 SampleCausticsDispersion(float2 uv, float2 dispersionOffset)
	{
		float center = SampleCaustics(uv);
		float3 dispersed = float3(
			SampleCaustics(uv - dispersionOffset * 0.75),
			center,
			SampleCaustics(uv + dispersionOffset));
		return lerp(center.xxx, dispersed, 0.5);
	}

	// Upstream's ComputeCaustics. worldPosition is absolute here, so unlike
	// upstream the UV does not re-add CameraPosAdjust.
	float3 ComputeCaustics(float waterHeight, float3 worldPosition)
	{
		float3 result = 1.0.xxx;

		float causticsDistToWater = waterHeight - worldPosition.z;
		float shoreFactorCaustics = saturate(causticsDistToWater / SHORE_RANGE);

		if (shoreFactorCaustics > 0.0) {
			float causticsFade = 1.0 - saturate(causticsDistToWater / FADE_RANGE);
			causticsFade *= causticsFade;

			float2 causticsUV = worldPosition.xy * UV_SCALE;
			float2 dispersionOffset = float2(0.6, 0.8) *
			                          (0.025 * shoreFactorCaustics *
			                              saturate(causticsDistToWater / DISPERSION_RANGE));

			float2 causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.2, 1.0);
			float2 causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.2, -0.5);
			const float3 causticsHigh =
				(causticsFade > 0.0) ?
					(min(SampleCausticsDispersion(causticsUV1, dispersionOffset),
						 SampleCausticsDispersion(causticsUV2, dispersionOffset)) *
						CAUSTICS_GAIN) :
					1.0.xxx;

			causticsUV *= 0.5;
			dispersionOffset *= 0.5;

			causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.1, 1.0);
			causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.1, -0.5);
			const float3 causticsLow =
				(causticsFade < 1.0) ?
					(min(SampleCausticsDispersion(causticsUV1, dispersionOffset),
						 SampleCausticsDispersion(causticsUV2, dispersionOffset)) *
						CAUSTICS_GAIN) :
					1.0.xxx;

			const float3 caustics = lerp(causticsLow, causticsHigh, causticsFade);
			result = lerp(1.0.xxx, caustics, shoreFactorCaustics);
		}

		return result;
	}

	// An unbound t32 samples as 0, which would darken rather than no-op. Fail
	// to identity instead.
	bool CausticsTextureReady()
	{
		uint2 causticsDims;
		WaterCaustics.GetDimensions(causticsDims.x, causticsDims.y);
		return !any(causticsDims == 0);
	}

	// FO4's directional terms are scalars, so consume the undispersed centre
	// channel. Dead dispersion taps fold away.
	float GetCausticsMult(float3 worldPosition)
	{
		if (SharedData::waterEffectsSettings.Mode == MODE_DISABLED ||
			SharedData::waterEffectsSettings.HasWater == 0 ||
			!CausticsTextureReady()) {
			return 1.0;
		}
		return ComputeCaustics(
			SharedData::waterEffectsSettings.WaterHeight, worldPosition)
		    .y;
	}

	float3 ViewToWorldPosition(
		float3 viewPosition,
		float4 viewToWorldRow0,
		float4 viewToWorldRow1,
		float4 viewToWorldRow2,
		float4 cameraPosAdjust)
	{
		float4 positionView = float4(viewPosition, 1.0);
		float3 worldPosition;
		worldPosition.x = dot(viewToWorldRow0, positionView) + cameraPosAdjust.x;
		worldPosition.y = dot(viewToWorldRow1, positionView) + cameraPosAdjust.y;
		worldPosition.z = dot(viewToWorldRow2, positionView) + cameraPosAdjust.z;
		return worldPosition;
	}

	float GetCausticsMultFromViewPosition(
		float3 viewPosition,
		float4 viewToWorldRow0,
		float4 viewToWorldRow1,
		float4 viewToWorldRow2,
		float4 cameraPosAdjust)
	{
		return GetCausticsMult(
			ViewToWorldPosition(
				viewPosition,
				viewToWorldRow0,
				viewToWorldRow1,
				viewToWorldRow2,
				cameraPosAdjust));
	}

	bool TryGetViewPositionFromScreenPosition(
		float2 pixelPosition,
		float4 farReprojRow0,
		float4 farReprojRow1,
		float4 farReprojRow2,
		float4 farReprojRow3,
		float4 nearReprojRow0,
		float4 nearReprojRow1,
		float4 nearReprojRow2,
		float4 nearReprojRow3,
		out float3 viewPosition)
	{
		viewPosition = 0.0;
		uint2 depthDimensions;
		SceneDepthTexture.GetDimensions(depthDimensions.x, depthDimensions.y);
		if (any(depthDimensions == 0))
			return false;

		uint2 depthPixel = min(uint2(pixelPosition), depthDimensions - 1);
		float rawDepth = SceneDepthTexture.Load(int3(depthPixel, 0));
		float4 position;
		float4 reprojRow0;
		float4 reprojRow1;
		float4 reprojRow2;
		float4 reprojRow3;
		if (rawDepth <= 0.01) {
			position.z = rawDepth * 100.0;
			reprojRow0 = nearReprojRow0;
			reprojRow1 = nearReprojRow1;
			reprojRow2 = nearReprojRow2;
			reprojRow3 = nearReprojRow3;
		} else {
			position.z = rawDepth * 1.01 - 0.01;
			reprojRow0 = farReprojRow0;
			reprojRow1 = farReprojRow1;
			reprojRow2 = farReprojRow2;
			reprojRow3 = farReprojRow3;
		}

		float2 renderUv =
			pixelPosition * SharedData::BufferDim.zw * SharedData::DynamicResolution.zw;
		position.xy = float2(renderUv.x, 1.0 - renderUv.y) * 2.0 - 1.0;
		position.w = 1.0;
		float4 viewPositionH;
		viewPositionH.x = dot(reprojRow0, position);
		viewPositionH.y = dot(reprojRow1, position);
		viewPositionH.z = dot(reprojRow2, position);
		viewPositionH.w = dot(reprojRow3, position);
		if (abs(viewPositionH.w) < 1e-6)
			return false;

		viewPosition = viewPositionH.xyz / viewPositionH.w;
		return true;
	}

	// Debug shows exactly what the light path consumes: the same scalar
	// multiplier, so the two views cannot disagree.
	bool TryGetDebugValue(float3 worldPosition, out float value)
	{
		value = 0.0;
		uint mode = SharedData::waterEffectsSettings.Mode;
		if (mode != MODE_CAUSTICS && mode != MODE_SUBMERSION)
			return false;
		if (SharedData::waterEffectsSettings.HasWater == 0)
			return true;

		float distToWater =
			SharedData::waterEffectsSettings.WaterHeight - worldPosition.z;
		if (distToWater <= 0.0)
			return true;

		if (mode == MODE_SUBMERSION) {
			value = saturate(distToWater / FADE_RANGE);
			return true;
		}

		value = saturate(
			ComputeCaustics(
				SharedData::waterEffectsSettings.WaterHeight, worldPosition)
					.y *
				0.25);
		return true;
	}

	bool TryGetDebugColorFromViewPosition(
		float3 viewPosition,
		float4 viewToWorldRow0,
		float4 viewToWorldRow1,
		float4 viewToWorldRow2,
		float4 cameraPosAdjust,
		out float4 color)
	{
		color = 0.0;
		uint mode = SharedData::waterEffectsSettings.Mode;
		if (mode != MODE_CAUSTICS && mode != MODE_SUBMERSION)
			return false;

		float value;
		const bool active = TryGetDebugValue(
			ViewToWorldPosition(
				viewPosition,
				viewToWorldRow0,
				viewToWorldRow1,
				viewToWorldRow2,
				cameraPosAdjust),
			value);
		color = float4(value.xxx, 1.0);
		return active;
	}

	bool TryGetDebugColorFromScreenPosition(
		float2 pixelPosition,
		float4 viewToWorldRow0,
		float4 viewToWorldRow1,
		float4 viewToWorldRow2,
		float4 cameraPosAdjust,
		float4 farReprojRow0,
		float4 farReprojRow1,
		float4 farReprojRow2,
		float4 farReprojRow3,
		float4 nearReprojRow0,
		float4 nearReprojRow1,
		float4 nearReprojRow2,
		float4 nearReprojRow3,
		out float4 color)
	{
		color = 0.0;
		float3 viewPosition;
		if (!TryGetViewPositionFromScreenPosition(
				pixelPosition,
				farReprojRow0,
				farReprojRow1,
				farReprojRow2,
				farReprojRow3,
				nearReprojRow0,
				nearReprojRow1,
				nearReprojRow2,
				nearReprojRow3,
				viewPosition))
			return false;

		return TryGetDebugColorFromViewPosition(
			viewPosition,
			viewToWorldRow0,
			viewToWorldRow1,
			viewToWorldRow2,
			cameraPosAdjust,
			color);
	}
}
