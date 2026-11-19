#pragma once

#include <functional>

#include <d3d12.h>

#include "Render/HdrCompose.h"
#include "Render/TemporalProvider.h"

namespace cs::render
{
	struct TemporalPresentationCallbacks
	{
		std::function<void()> clearCapture;
		std::function<void(const char*)> recordFailure;
		std::function<render::temporal::FrameGenerationRequest()> queryFrameState;
		std::function<void(ID3D12Device*, HWND)> bindD3D12CaptureTarget;
		std::function<void(ID3D12Device*)> unbindD3D12CaptureTarget;
		// On publish and whenever the swap chain is rebuilt or released.
		std::function<void(const render::temporal::PresentationBinding&)> bindPresentation;
		// Rollback only, after the swap chain is gone.
		std::function<void()> unbindPresentation;
		// Behind a D3D12 presenter the D3D11 device has no chain; frames end here.
		std::function<void()> gameFramePresented;
		// Set when HDR Display is loaded; the chain then becomes the HDR container.
		std::function<hdr::OutputSettings()> queryHdrOutput;
	};
}  // namespace cs::render
