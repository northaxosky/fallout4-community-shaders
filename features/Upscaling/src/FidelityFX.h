#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include "IUpscalerBackend.h"

#include <memory>

namespace cs::features::upscaling
{

class FidelityFX : public IUpscalerBackend
{
public:
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	bool IsAvailable() const override { return true; }
	void CreateResources() override { CreateFSRResources(); }
	void DestroyResources() override { DestroyFSRResources(); }
	void PrepareReactiveMask() override { GenerateReactiveMask(); }
	void Upscale(Texture2D* a_color, Texture2D*, Texture2D* a_reactiveMask, Texture2D* a_transparencyMask,
		float2 a_jitter, float2 a_renderSize, uint) override
	{
		Upscale(a_color, a_reactiveMask, a_transparencyMask, a_jitter, a_renderSize);
	}

	void CreateFSRResources();

	void DestroyFSRResources();

	void GenerateReactiveMask();

	void Upscale(Texture2D* a_color, Texture2D* a_reactiveMask, Texture2D* a_transparencyMask,
		float2 a_jitter, float2 a_renderSize);

	FfxFsr3Context fsrContext;

private:
	void* fsrScratchBuffer = nullptr;
};

}
