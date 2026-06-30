#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include "Buffer.h"

#include <memory>

namespace cs::features::upscaling
{

// FSR3 upscaling manager with reactive-mask support.
class FidelityFX
{
public:
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	// Creates the FSR3 context, scratch buffer, opaque color, and reactive mask resources.
	void CreateFSRResources();

	// Destroys the FSR3 context and releases all FSR-owned resources.
	void DestroyFSRResources();

	// Captures opaque color before transparency so FSR3 can build a reactive mask.
	void CopyOpaqueTexture();

	// Builds the FSR3 reactive mask after transparency.
	void GenerateReactiveMask();

	// Runs FSR3 temporal upscaling from render to display resolution.
	void Upscale(Texture2D* a_color, float2 a_jitter, float2 a_renderSize);

	FfxFsr3Context fsrContext;  ///< FSR3 context handle

	std::unique_ptr<Texture2D> colorOpaqueOnlyTexture;  ///< Color before transparent objects
	std::unique_ptr<Texture2D> reactiveMaskTexture;     ///< Generated reactive mask for FSR3

private:
	void* fsrScratchBuffer = nullptr;  ///< FSR3 backend scratch memory (freed in DestroyFSRResources)
};

}
