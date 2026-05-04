#pragma once

#include <dx12/ffx_api_dx12.h>
#include <ffx_api.hpp>
#include <ffx_api_loader.h>
#include <ffx_api_types.h>
#include <ffx_framegeneration.hpp>

#include "Buffer.h"

namespace cs::features::FrameGeneration
{

class FidelityFX
{
public:
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	HMODULE module = nullptr;

	ffx::Context swapChainContext{};
	ffx::Context frameGenContext;

	void LoadFFX();
	void SetupFrameGeneration();
	void Present(bool a_useFrameGeneration);
};

}
