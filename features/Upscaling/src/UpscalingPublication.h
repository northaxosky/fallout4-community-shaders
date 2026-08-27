#pragma once

#include <d3d11.h>

#include "Render/RendererContext.h"

namespace cs::features
{
	[[nodiscard]] inline bool PublishUpscalingOutput(
		ID3D11DeviceContext* a_context,
		ID3D11Resource* a_frameBuffer,
		ID3D11Resource* a_providerOutput,
		bool a_providerSucceeded) noexcept
	{
		if (!a_providerSucceeded || !a_context || !a_frameBuffer || !a_providerOutput ||
			a_frameBuffer == a_providerOutput) {
			return false;
		}

		cs::engine::CopyResourcePreservingOM(a_context, a_frameBuffer, a_providerOutput);
		return true;
	}
}
