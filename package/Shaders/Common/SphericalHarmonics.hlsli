// SPDX-License-Identifier: MIT
// Copyright (c) 2018 SebH
// SphericalHarmonics.hlsl from https://github.com/sebh/HLSL-Spherical-Harmonics
// [4]  https://d3cw3dd2w32x2b.cloudfront.net/wp-content/uploads/2011/06/10-14.pdf

#ifndef __SPHERICAL_HARMONICS_DEPENDENCY_HLSL__
#define __SPHERICAL_HARMONICS_DEPENDENCY_HLSL__

#define sh2 float4

namespace SphericalHarmonics
{
	sh2 Evaluate(float3 dir)
	{
		sh2 result;
		result.x = 0.28209479177387814347403972578039f;
		result.y = -0.48860251190291992158638462283836f * dir.y;
		result.z = 0.48860251190291992158638462283836f * dir.z;
		result.w = -0.48860251190291992158638462283836f * dir.x;
		return result;
	}

	sh2 EvaluateCosineLobe(float3 dir)
	{
		sh2 result;
		result.x = 0.8862269254527580137f;
		result.y = -1.0233267079464884885f * dir.y;
		result.z = 1.0233267079464884885f * dir.z;
		result.w = -1.0233267079464884885f * dir.x;
		return result;
	}

	float FuncProductIntegral(sh2 shL, sh2 shR)
	{
		return dot(shL, shR);
	}

	float SHHallucinateZH3Irradiance(sh2 inSH, float3 direction)
	{
		float3 zonalAxis = normalize(float3(inSH.w, inSH.y, inSH.z));
		float ratio = 0.0;
		ratio = abs(dot(float3(-inSH.w, -inSH.y, inSH.z), zonalAxis));
		ratio /= inSH.x;
		float zonalL2Coeff = inSH.x * (0.08f * ratio + 0.6f * ratio * ratio);
		float fZ = dot(zonalAxis, direction);
		float zhDir = sqrt(5.0f / (16.0f * 3.14159265358979323846f)) * (3.0f * fZ * fZ - 1.0f);
		float result = SphericalHarmonics::FuncProductIntegral(inSH, SphericalHarmonics::EvaluateCosineLobe(direction));
		result += 0.25f * zonalL2Coeff * zhDir;
		return max(0, result);
	}
}

#endif
