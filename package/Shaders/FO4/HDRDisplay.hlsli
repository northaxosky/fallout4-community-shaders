// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#ifndef FO4_HDR_DISPLAY_HLSLI
#define FO4_HDR_DISPLAY_HLSLI

#ifdef HDR_OUTPUT
#	include "Common/SharedData.hlsli"

namespace FO4HDR
{
	// HDRData.x gates HDR; w > 0.9 marks main menu and loading screens, kept SDR.
	bool IsActive()
	{
		float4 data = SharedData::HDRData;
		return data.x > 0.5 && data.w <= 0.9;
	}
}
#endif
#endif
