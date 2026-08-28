// SPDX-License-Identifier: MIT
// Copyright (c) 2022 Ilya Perapechka
// Ported from Skyrim Community Shaders.

#ifndef __MATH_DEPENDENCY_HLSL__
#define __MATH_DEPENDENCY_HLSL__

#define EPSILON_DIVISION 1e-6f
#define EPSILON_WEIGHT_SUM 1e-10f
#define EPSILON_LENGTH_SQ 1e-20f

namespace Math
{
	static const float PI = 3.1415926535897932384626433832795f;
	static const float HALF_PI = PI * 0.5f;
	static const float INV_PI = 1.0f / PI;
}

#endif  //__MATH_DEPENDENCY_HLSL__
