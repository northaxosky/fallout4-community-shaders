// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#pragma once

#include "Common/SharedData.hlsli"

namespace ExponentialHeightFog
{
	static const uint MODE_ENABLED = 1U << 0;
	static const uint MODE_FOG_FACTOR_DEBUG = 1U << 1;
	static const float MINIMUM_SLOPE = 1.0e-6;
	static const float MINIMUM_DISTANCE_SPAN = 1.0e-3;
	static const float REFERENCE_OPTICAL_DEPTH = 4.605170186;

	bool IsActive()
	{
		return (SharedData::exponentialHeightFogSettings.Mode & MODE_ENABLED) != 0
			&& !SharedData::InInterior;
	}

	bool IsFogFactorDebug()
	{
		return IsActive()
			&& (SharedData::exponentialHeightFogSettings.Mode
				& MODE_FOG_FACTOR_DEBUG) != 0;
	}

	bool TryHeightFactor(
		float height,
		float scale,
		float bias,
		float multiplier,
		out float factor)
	{
		factor = 0.0;
		if (!isfinite(height) || !isfinite(scale) || !isfinite(bias)
			|| !isfinite(multiplier) || abs(scale) <= MINIMUM_SLOPE
			|| !(multiplier > 0.0)) {
			return false;
		}

		float zeroHeight = bias / scale;
		float falloff = REFERENCE_OPTICAL_DEPTH * abs(scale) * multiplier;
		float directedAltitude =
			max((height - zeroHeight) * (scale < 0.0 ? -1.0 : 1.0), 0.0);
		float opticalDepth = falloff * directedAltitude;
		if (!isfinite(zeroHeight) || !isfinite(falloff)
			|| !isfinite(opticalDepth)) {
			return false;
		}

		factor = saturate(1.0 - exp(-opticalDepth));
		return isfinite(factor);
	}

	bool TryEvaluate(
		float distance,
		float height,
		float4 distanceRamp,
		float4 heightRamp,
		out float distanceExtinction,
		out float2 heightFactors)
	{
		distanceExtinction = 0.0;
		heightFactors = 0.0;
		if (!IsActive())
			return false;

		float densityMultiplier =
			SharedData::exponentialHeightFogSettings.DensityMultiplier;
		float heightMultiplier =
			SharedData::exponentialHeightFogSettings.HeightFalloffMultiplier;
		if (!isfinite(distance) || !isfinite(distanceRamp.x)
			|| !isfinite(distanceRamp.z) || !isfinite(densityMultiplier)
			|| abs(distanceRamp.x) <= MINIMUM_SLOPE
			|| !(densityMultiplier > 0.0)) {
			return false;
		}

		float nearDistance = distanceRamp.z / distanceRamp.x;
		float farDistance = (1.0 + distanceRamp.z) / distanceRamp.x;
		float distanceSpan = farDistance - nearDistance;
		if (!isfinite(nearDistance) || !isfinite(farDistance)
			|| !isfinite(distanceSpan)
			|| !(distanceSpan > MINIMUM_DISTANCE_SPAN)) {
			return false;
		}

		float density =
			REFERENCE_OPTICAL_DEPTH / distanceSpan * densityMultiplier;
		float opticalDepth = density * max(distance - nearDistance, 0.0);
		if (!isfinite(density) || !isfinite(opticalDepth)
			|| !(density > 0.0)) {
			return false;
		}

		float heightFactorX;
		float heightFactorY;
		if (!TryHeightFactor(
				height,
				heightRamp.x,
				heightRamp.z,
				heightMultiplier,
				heightFactorX)
			|| !TryHeightFactor(
				height,
				heightRamp.y,
				heightRamp.w,
				heightMultiplier,
				heightFactorY)) {
			return false;
		}

		distanceExtinction = saturate(1.0 - exp(-opticalDepth));
		heightFactors = float2(heightFactorX, heightFactorY);
		return isfinite(distanceExtinction) && all(isfinite(heightFactors));
	}
}
