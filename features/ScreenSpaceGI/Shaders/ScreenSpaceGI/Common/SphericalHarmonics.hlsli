// SPDX-License-Identifier: MIT
// Copyright (c) 2018 SebH
// SphericalHarmonics.hlsl from https://github.com/sebh/HLSL-Spherical-Harmonics
// [4]  https://d3cw3dd2w32x2b.cloudfront.net/wp-content/uploads/2011/06/10-14.pdf

#ifndef __SPHERICAL_HARMONICS_DEPENDENCY_HLSL__
#define __SPHERICAL_HARMONICS_DEPENDENCY_HLSL__

#include "Math.hlsli"

#define sh2 float4

namespace SphericalHarmonics
{
	// Evaluates spherical harmonics basis for a direction dir.
	sh2 Evaluate(float3 dir)
	{
		sh2 result;
		result.x = 0.28209479177387814347403972578039f;           // L=0 , M= 0
		result.y = -0.48860251190291992158638462283836f * dir.y;  // L=1 , M=-1
		result.z = 0.48860251190291992158638462283836f * dir.z;   // L=1 , M= 0
		result.w = -0.48860251190291992158638462283836f * dir.x;  // L=1 , M= 1
		return result;
	}

	// Projects a cosine lobe function, with peak value in direction dir, into SH. (from [4])
	sh2 EvaluateCosineLobe(float3 dir)
	{
		sh2 result;
		result.x = 0.8862269254527580137f;           // L=0 , M= 0
		result.y = -1.0233267079464884885f * dir.y;  // L=1 , M=-1
		result.z = 1.0233267079464884885f * dir.z;   // L=1 , M= 0
		result.w = -1.0233267079464884885f * dir.x;  // L=1 , M= 1
		return result;
	}

	// Integrates the product of two SH functions over the unit sphere.
	float FuncProductIntegral(sh2 shL, sh2 shR)
	{
		return dot(shL, shR);
	}

	// Hallucinate zonal harmonics for diffuse lighting with more contrast
	// http://torust.me/ZH3.pdf
	float SHHallucinateZH3Irradiance(sh2 inSH, float3 direction)
	{
		float3 zonalAxis = normalize(float3(inSH.w, inSH.y, inSH.z));
		float ratio = 0.0;
		ratio = abs(dot(float3(-inSH.w, -inSH.y, inSH.z), zonalAxis));
		ratio /= inSH.x;
		float zonalL2Coeff = inSH.x * (0.08f * ratio + 0.6f * ratio * ratio);  // Curve-fit; Section 3.4.3
		float fZ = dot(zonalAxis, direction);
		float zhDir = sqrt(5.0f / (16.0f * Math::PI)) * (3.0f * fZ * fZ - 1.0f);
		float result = SphericalHarmonics::FuncProductIntegral(inSH, SphericalHarmonics::EvaluateCosineLobe(direction));
		result += 0.25f * zonalL2Coeff * zhDir;
		return max(0, result);
	}
}

#endif  // __SPHERICAL_HARMONICS_DEPENDENCY_HLSL__
