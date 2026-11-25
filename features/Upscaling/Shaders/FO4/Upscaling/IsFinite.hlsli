// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FO4_UPSCALING_IS_FINITE_HLSLI
#define FO4_UPSCALING_IS_FINITE_HLSLI

// Bit test: fast-math folds isfinite away.
bool IsFinite(float3 a_value)
{
	return all((asuint(a_value) & 0x7F800000) != 0x7F800000);
}

bool IsFinite(float4 a_value)
{
	return all((asuint(a_value) & 0x7F800000) != 0x7F800000);
}

#endif
