// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#pragma once

#include "Common/SphericalHarmonics.hlsli"

namespace Skylighting
{
	static const uint MODE_ENABLED = 1U << 0;
	static const uint MODE_VISIBILITY_DEBUG = 1U << 1;
	static const uint3 ARRAY_DIM = uint3(256, 256, 128);
	static const float PI = 3.14159265358979323846;
	static const sh2 UNIT_SH = float4(sqrt(4.0 * PI), 0.0, 0.0, 0.0);

	cbuffer SkylightingData : register(b7)
	{
		row_major float4x4 OcclusionViewProj;
		float4 OcclusionDirection;
		float4 ViewToWorld_row0;
		float4 ViewToWorld_row1;
		float4 ViewToWorld_row2;
		float4 CameraPosAdjust;
		float4 PosOffset;
		uint4 ArrayOrigin;
		int4 ValidMargin;
		float OcclusionExtent;
		float MinDiffuseVisibility;
		float MinSpecularVisibility;
		uint Mode;
	};

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

	float3 ReconstructWorldDirection(float3 directionView)
	{
		return normalize(float3(
			dot(ViewToWorld_row0.xyz, directionView),
			dot(ViewToWorld_row1.xyz, directionView),
			dot(ViewToWorld_row2.xyz, directionView)));
	}

	float3 GetArraySize()
	{
		return OcclusionExtent * float3(1.0, 1.0, 0.5);
	}

	float3 GetCellSize()
	{
		return GetArraySize() / ARRAY_DIM;
	}

	float GetFadeOutFactor(float3 positionMSAdjusted)
	{
		float3 uvw = saturate(positionMSAdjusted / GetArraySize() + 0.5);
		float3 distances = min(uvw, 1.0 - uvw);
		float edgeDistance = min(distances.x, min(distances.y, distances.z));
		return saturate(edgeDistance * 20.0);
	}

	float CosineLobeVisibility(
		sh2 probeSH, float3 normal, float fadeOutFactor)
	{
		float visibility = SphericalHarmonics::FuncProductIntegral(
			probeSH, SphericalHarmonics::EvaluateCosineLobe(normal)) /
			PI;
		return lerp(1.0, saturate(visibility), fadeOutFactor);
	}

	sh2 SampleProbeArray(float3 positionMS, float3 normalWS)
	{
		positionMS += normalWS * GetCellSize() * 0.5;
		float3 positionMSAdjusted = positionMS - PosOffset.xyz;
		float3 uvw = positionMSAdjusted / GetArraySize() + 0.5;
		if (any(uvw < 0.0) || any(uvw > 1.0))
			return UNIT_SH;

		float3 cellVxCoord = uvw * ARRAY_DIM;
		int3 cell000 = (int3)floor(cellVxCoord - 0.5);
		float3 trilinearPos = cellVxCoord - 0.5 - cell000;
		sh2 shSum = 0.0;
		float shWeightSum = 0.0;

		[unroll]
		for (int i = 0; i < 2; ++i) {
			[unroll]
			for (int j = 0; j < 2; ++j) {
				[unroll]
				for (int k = 0; k < 2; ++k) {
					int3 cellOffset = int3(i, j, k);
					int3 cellID = cell000 + cellOffset;
					if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
						continue;

					float3 cellCentreMS =
						(cellID + 0.5 - ARRAY_DIM / 2) * GetCellSize();
					float3 trilinearWeights =
						1.0 - abs(cellOffset - trilinearPos);
					float trilinearWeight =
						trilinearWeights.x * trilinearWeights.y *
						trilinearWeights.z;
					float tangentWeight =
						dot(
							normalize(cellCentreMS - positionMSAdjusted),
							normalWS) *
							0.5 +
						0.5;
					float shWeight = trilinearWeight * tangentWeight;
					uint3 cellTextureID =
						(uint3)(cellID + ArrayOrigin.xyz) % ARRAY_DIM;
					shSum += ProbeArray[cellTextureID] * shWeight;
					shWeightSum += shWeight;
				}
			}
		}

		return shSum / max(shWeightSum, 1.0e-6);
	}

	Evaluation Evaluate(float3 positionView, float3 normalView)
	{
		Evaluation result;
		result.visibility = 1.0;
		result.diffuse = 1.0;
		result.specular = 1.0;
		if (!IsEnabled())
			return result;

		float3 positionCameraRelative =
			ReconstructCameraRelativePosition(positionView);
		float3 normalWorld = ReconstructWorldDirection(normalView);
		float3 positionMSAdjusted = positionCameraRelative - PosOffset.xyz;
		sh2 probeSH = SampleProbeArray(positionCameraRelative, normalWorld);
		result.visibility = CosineLobeVisibility(
			probeSH,
			normalWorld,
			GetFadeOutFactor(positionMSAdjusted));

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
