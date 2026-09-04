// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#pragma once

namespace Skylighting
{
	Texture2D<float> OcclusionDepth : register(t3);
	SamplerComparisonState OcclusionComparisonSampler : register(s0);
}
