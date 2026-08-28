/******************************************************************************
    The MIT License (MIT)
    Copyright (c) <2014> <Michal Drobot>
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
********************************************************************************/

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
