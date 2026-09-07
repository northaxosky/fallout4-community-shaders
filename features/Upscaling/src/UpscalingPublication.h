#pragma once

#include <d3d11.h>

#include "Render/Annotation.h"
#include "Render/RendererContext.h"

namespace cs::features
{
	[[nodiscard]] inline bool PrepareUpscalingPassthrough(
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_privateOutput,
		ID3D11Texture2D* a_input) noexcept
	{
		if (!a_context || !a_privateOutput || !a_input ||
			a_privateOutput == a_input) {
			return false;
		}

		D3D11_TEXTURE2D_DESC outputDesc{};
		D3D11_TEXTURE2D_DESC inputDesc{};
		a_privateOutput->GetDesc(&outputDesc);
		a_input->GetDesc(&inputDesc);
		if (outputDesc.Width != inputDesc.Width ||
			outputDesc.Height != inputDesc.Height ||
			outputDesc.MipLevels != inputDesc.MipLevels ||
			outputDesc.ArraySize != inputDesc.ArraySize ||
			outputDesc.Format != inputDesc.Format ||
			outputDesc.SampleDesc.Count != inputDesc.SampleDesc.Count ||
			outputDesc.SampleDesc.Quality != inputDesc.SampleDesc.Quality) {
			return false;
		}

		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/PreparePassthrough");
		cs::engine::CopyResourcePreservingOM(
			a_context, a_privateOutput, a_input);
		return true;
	}

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

		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/PublishOutput");
		cs::engine::CopyResourcePreservingOM(a_context, a_frameBuffer, a_providerOutput);
		return true;
	}
}
