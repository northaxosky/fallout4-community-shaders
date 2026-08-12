// SPDX-License-Identifier: MIT
// Copyright (c) 2014 Michal Drobot

#ifndef SHADER_FAST_MATH_INC_FX
#define SHADER_FAST_MATH_INC_FX

#include "Math.hlsli"

#define IEEE_INT_SQRT_CONST_NR0 0x1FBD1DF5

namespace FastMath
{
	float sqrtIEEEIntApproximation(float inX, const int inSqrtConst)
	{
		int x = asint(inX);
		x = inSqrtConst + (x >> 1);
		return asfloat(x);
	}

	float fastSqrtNR0(float inX)
	{
		float xRcp = sqrtIEEEIntApproximation(inX, IEEE_INT_SQRT_CONST_NR0);
		return xRcp;
	}

	float ACos(float inX)
	{
		float x = abs(inX);
		float res = -0.156583f * x + Math::HALF_PI;
		res *= fastSqrtNR0(1.0f - x);
		return (inX >= 0) ? res : Math::PI - res;
	}
}

#endif  // SHADER_FAST_MATH_INC_FX
