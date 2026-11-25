#pragma once

// Public C ABI. Copy this header; an export missing by name is unsupported.

#include <stdint.h>

#ifndef __cplusplus
#	include <stdbool.h>
#endif

// What a replacement tone mapper must write for the gamma/LUT pass.
enum CS_HdrSceneEncoding
{
	// Linear BT.709, 1.0 at paper white, mapped below peakNits / paperWhiteNits.
	CS_HdrSceneEncoding_LinearPaperWhite = 0,
};

// What a replacement gamma/LUT pass must write for the presenter.
enum CS_HdrLutOutputEncoding
{
	// sign(x) * pow(abs(x), 1 / 2.2), BT.709, 1.0 at paper white.
	CS_HdrLutOutputEncoding_SignedGamma22PaperWhite = 0,
};

// Append-only; fields past the returned size are not written.
struct CS_HdrOutputState
{
	uint32_t size;       // in: sizeof the caller's struct; out: bytes written
	uint32_t hdrActive;  // 1 while the frame renders HDR
	float paperWhiteNits;
	float peakNits;
	uint32_t sceneEncoding;      // CS_HdrSceneEncoding
	uint32_t lutOutputEncoding;  // CS_HdrLutOutputEncoding
};

// Export "CS_GetHdrOutputState"; render thread only, false if size < 16.
typedef bool (*CS_GetHdrOutputStateFn)(struct CS_HdrOutputState* a_state);
