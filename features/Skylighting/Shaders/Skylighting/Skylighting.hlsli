// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#pragma once

namespace Skylighting
{
	static const uint MODE_ENABLED = 1U << 0;
	static const uint MODE_VISIBILITY_DEBUG = 1U << 1;

	cbuffer SkylightingData : register(b7)
	{
		row_major float4x4 OcclusionViewProj;
		float4 OcclusionDirection;
		float4 ViewToWorld_row0;
		float4 ViewToWorld_row1;
		float4 ViewToWorld_row2;
		float4 CameraPosAdjust;
		float OcclusionExtent;
		float MinDiffuseVisibility;
		float MinSpecularVisibility;
		uint Mode;
	};

	Texture2D<float> OcclusionDepth : register(t9);
	SamplerComparisonState OcclusionComparisonSampler : register(s9);
	RWByteAddressBuffer FootprintTelemetry : register(u7);

	struct Evaluation
	{
		float visibility;
		float diffuse;
		float specular;
	};

	bool IsEnabled()
	{
		return (Mode & MODE_ENABLED) != 0;
	}

	bool IsVisibilityDebug()
	{
		return (Mode & MODE_VISIBILITY_DEBUG) != 0;
	}

	float3 ReconstructCameraRelativePosition(float3 positionView)
	{
		float4 positionViewH = float4(positionView, 1.0);
		return float3(
			dot(ViewToWorld_row0, positionViewH),
			dot(ViewToWorld_row1, positionViewH),
			dot(ViewToWorld_row2, positionViewH));
	}

	float3 ReconstructWorldPosition(float3 positionView)
	{
		return ReconstructCameraRelativePosition(positionView) +
			CameraPosAdjust.xyz;
	}

	void CountFootprintSample(bool insideFootprint)
	{
		uint previousCount;
		FootprintTelemetry.InterlockedAdd(
			insideFootprint ? 0 : 4, 1, previousCount);
	}

	Evaluation Evaluate(float3 positionView)
	{
		Evaluation result;
		result.visibility = 1.0;
		result.diffuse = 1.0;
		result.specular = 1.0;
		if (!IsEnabled())
			return result;

		float3 positionCameraRelative =
			ReconstructCameraRelativePosition(positionView);
		float4 positionOcclusion = mul(
			OcclusionViewProj, float4(positionCameraRelative, 1.0));
		if (!all(isfinite(positionOcclusion)) ||
			abs(positionOcclusion.w) <= 1.0e-6) {
			CountFootprintSample(false);
			return result;
		}

		positionOcclusion.xyz /= positionOcclusion.w;
		if (!all(isfinite(positionOcclusion.xyz))) {
			CountFootprintSample(false);
			return result;
		}
		positionOcclusion.y = -positionOcclusion.y;
		float2 uv = positionOcclusion.xy * 0.5 + 0.5;
		if (any(uv <= 0.0) || any(uv >= 1.0) ||
			positionOcclusion.z < 0.0 || positionOcclusion.z > 1.0) {
			CountFootprintSample(false);
			return result;
		}
		CountFootprintSample(true);

		float sampledVisibility =
			OcclusionDepth.SampleCmpLevelZero(
				OcclusionComparisonSampler, uv, positionOcclusion.z);
		float2 edgeDistance = min(uv, 1.0 - uv);
		float footprintFade =
			saturate(min(edgeDistance.x, edgeDistance.y) * 20.0);
		result.visibility = lerp(
			1.0, saturate(sampledVisibility), footprintFade);

		float diffuseFloor = saturate(MinDiffuseVisibility);
		float specularFloor = saturate(MinSpecularVisibility);
		float diffuseVisibility =
			lerp(diffuseFloor, 1.0, result.visibility);
		float specularVisibility =
			lerp(specularFloor, 1.0, result.visibility);
		result.diffuse = diffuseVisibility;
		result.specular = specularVisibility;
		return result;
	}

	void ApplyAmbient(
		inout float3 diffuse,
		inout float3 specular,
		Evaluation evaluation)
	{
		if (!IsEnabled())
			return;
		diffuse *= evaluation.diffuse;
		specular *= evaluation.specular;
	}

	void ApplyFullscreenDebug(
		inout float4 diffuse,
		inout float4 specular,
		Evaluation evaluation)
	{
		if (!IsVisibilityDebug())
			return;
		diffuse = float4(evaluation.visibility.xxx / 3.0, 0.0);
		specular = float4(0.0, 0.0, 0.0, 1.0);
	}

	void DiscardNonConsumerDebug()
	{
#ifndef FO4_SKYLIGHTING_AMBIENT_CONSUMER
		if (IsVisibilityDebug())
			discard;
#endif
	}
}
