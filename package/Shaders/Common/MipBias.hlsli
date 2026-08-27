// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __MIP_BIAS_DEPENDENCY_HLSL__
#define __MIP_BIAS_DEPENDENCY_HLSL__

#include "SharedData.hlsli"

// Upstream biases material sampling by log2(render / display) while an upscaler is running,
// so textures keep their apparent sharpness at the reduced render resolution. The bias is
// published on the shared substrate; without it the bias is zero and SampleBias(s, uv, 0)
// is exactly the stock Sample(s, uv).
#ifdef FO4CS_SUBSTRATE
#	define FO4CS_MIP_BIAS SharedData::MipBias
#else
#	define FO4CS_MIP_BIAS 0.0
#endif

#endif  // __MIP_BIAS_DEPENDENCY_HLSL__
