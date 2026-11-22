// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FO4_UPSCALING_MATERIAL_CLASS_HLSLI
#define FO4_UPSCALING_MATERIAL_CLASS_HLSLI

// Water stencil passes write classes 2 and 3 into kGBufferAmbient alpha.
bool IsWaterClass(float a_classAlpha)
{
	const uint materialClass = (uint)round(a_classAlpha * 255.0);
	return materialClass == 2 || materialClass == 3;
}

#endif
