// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#ifndef __EXTENDED_TRANSLUCENCY_DEPENDENCY_HLSL__
#define __EXTENDED_TRANSLUCENCY_DEPENDENCY_HLSL__

#include "Common/SharedData.hlsli"

namespace ExtendedTranslucency
{
	static const uint MaterialDefault = 0;
	static const uint MaterialRimLight = 1;
	static const uint MaterialIsotropicFabric = 2;
	static const uint MaterialAnisotropicFabric = 3;
	static const uint MaterialDisabled = 4;

	static const uint MaterialMask = 7;
	static const uint DescriptorShift = 3;
	static const uint SourceShift = 6;
	static const uint DebugFlag = 1U << 8;
	static const uint SourceNone = 0;
	static const uint SourceExtraData = 1;
	static const uint SourceMaterialName = 2;
	static const float MinimumAlpha = 0.0156862754;

	uint GetDefaultMaterial(uint mode)
	{
		return mode & MaterialMask;
	}

	uint GetDescriptor(uint mode)
	{
		return (mode >> DescriptorShift) & MaterialMask;
	}

	uint GetSource(uint mode)
	{
		return (mode >> SourceShift) & 3;
	}

	bool IsValidMaterial(uint material)
	{
		return material > MaterialDefault && material < MaterialDisabled;
	}

	float GetViewDependentAlphaNaive(
		float alpha,
		float3 view,
		float3 normal)
	{
		return 1.0 - (1.0 - alpha) * dot(view, normal);
	}

	float GetViewDependentAlphaFabric1D(
		float alpha,
		float3 view,
		float3 normal)
	{
		return alpha / min(1.0, abs(dot(view, normal)) + 0.001);
	}

	float GetViewDependentAlphaFabric2D(
		float alpha,
		float3 view,
		float3x3 tangentBasis)
	{
		float3 tangent = tangentBasis[0];
		float3 bitangent = tangentBasis[1];
		float3 normal = tangentBasis[2];
		float alpha0 = 1.0 - sqrt(1.0 - alpha);
		return alpha0
				* (length(cross(view, tangent))
				   + length(cross(view, bitangent)))
				/ (abs(dot(view, normal)) + 0.001)
			- alpha0 * alpha0;
	}

	float SoftClamp(float alpha, float limit)
	{
		alpha = min(
			alpha,
			limit / (1.0 + exp(-4.0 * (alpha - limit * 0.5) / limit)));
		return saturate(alpha);
	}

	float ApplyAlpha(
		float alpha,
		float3 view,
		float3 normal,
		float3x3 tangentBasis,
		bool hasTangentBasis)
	{
		uint mode = SharedData::extendedTranslucencySettings.PackedMode;
		uint source = GetSource(mode);
		if (source == SourceNone || alpha < MinimumAlpha || alpha >= 1.0)
			return alpha;

		uint material = GetDescriptor(mode);
		float reduction = 0.0;
		float softness = 0.0;
		float strength = 0.0;
		if (material == MaterialDefault) {
			material = GetDefaultMaterial(mode);
			reduction =
				SharedData::extendedTranslucencySettings.AlphaReduction;
			softness =
				SharedData::extendedTranslucencySettings.AlphaSoftness;
			strength =
				SharedData::extendedTranslucencySettings.AlphaStrength;
		}
		if (!IsValidMaterial(material))
			return alpha;

		float originalAlpha = alpha;
		alpha *= 1.0 - saturate(reduction);
		if (material == MaterialAnisotropicFabric && hasTangentBasis) {
			alpha = GetViewDependentAlphaFabric2D(
				alpha, view, tangentBasis);
		} else if (
			material == MaterialAnisotropicFabric
			|| material == MaterialIsotropicFabric) {
			alpha = GetViewDependentAlphaFabric1D(alpha, view, normal);
		} else {
			alpha = GetViewDependentAlphaNaive(alpha, view, normal);
		}
		alpha = SoftClamp(
			alpha,
			2.0 - saturate(softness));
		return lerp(
			alpha,
			originalAlpha,
			saturate(strength));
	}

	bool TryGetDebugColor(out float3 color)
	{
		uint mode = SharedData::extendedTranslucencySettings.PackedMode;
		if ((mode & DebugFlag) == 0) {
			color = 0.0;
			return false;
		}

		uint source = GetSource(mode);
		if (source == SourceExtraData) {
			uint descriptor = GetDescriptor(mode);
			color = IsValidMaterial(descriptor)
				? float3(0.1, 1.0, 0.1)
				: float3(1.0, 0.1, 0.1);
			return true;
		}
		if (source == SourceMaterialName) {
			color = float3(0.1, 1.0, 1.0);
			return true;
		}

		color = 0.0;
		return false;
	}

	float4 ApplyToColor(
		float4 color,
		float alpha,
		float3 view,
		float3 normal,
		float3x3 tangentBasis)
	{
		color.w = ApplyAlpha(
			alpha, view, normal, tangentBasis, true);
		float3 debugColor;
		if (TryGetDebugColor(debugColor))
			color.xyz = debugColor;
		return color;
	}

	float4 ApplyToColorWithoutTangent(
		float4 color,
		float alpha,
		float3 view,
		float3 normal)
	{
		color.w = ApplyAlpha(
			alpha, view, normal, (float3x3)0.0, false);
		float3 debugColor;
		if (TryGetDebugColor(debugColor))
			color.xyz = debugColor;
		return color;
	}
}

#endif
