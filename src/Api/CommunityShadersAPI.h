#pragma once

// Public C interface of FO4CommunityShaders.dll. Other plugins copy this header
// and resolve each export by name; a missing export means it is unsupported.

#include <stdint.h>

#ifndef __cplusplus
#	include <stdbool.h>
#endif

// What a replacement tone mapper must write: the image the gamma/LUT pass reads.
enum CS_HdrSceneEncoding
{
	// Linear BT.709, 1.0 at paper white, mapped below peakNits / paperWhiteNits.
	CS_HdrSceneEncoding_LinearPaperWhite = 0,
};

// What a replacement gamma/LUT pass must write: the image the presenter decodes.
enum CS_HdrLutOutputEncoding
{
	// sign(x) * pow(abs(x), 1 / 2.2), BT.709, 1.0 at paper white.
	CS_HdrLutOutputEncoding_SignedGamma22PaperWhite = 0,
};

// HDR output state for a plugin that renders the scene this presenter encodes.
// Append-only: fields past the caller's size are not written.
struct CS_HdrOutputState
{
	uint32_t size;       // sizeof the caller's struct, set by the caller
	uint32_t hdrActive;  // 1 while the frame renders HDR
	float paperWhiteNits;
	float peakNits;
	uint32_t sceneEncoding;      // CS_HdrSceneEncoding
	uint32_t lutOutputEncoding;  // CS_HdrLutOutputEncoding
};

// Export "CS_GetHdrOutputState"; false for a null or too-small a_state.
// Valid once device creation returns; callable from the render thread.
typedef bool (*CS_GetHdrOutputStateFn)(struct CS_HdrOutputState* a_state);
