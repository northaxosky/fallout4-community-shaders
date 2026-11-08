#pragma once

#include "Render/HdrCompose.h"
#include "Utils/CSBuffer.h"

#include <memory>

namespace cs::features::hdr_display
{
	// Encodes the split scene and interface for a screenshot to read.
	class CaptureComposer
	{
	public:
		// R10 PQ or R8 gamma 2.2; null unless the frame is split.
		[[nodiscard]] const buffer::Texture2D* Compose(
			const render::hdr::OutputSettings& a_settings, bool a_interface);

	private:
		[[nodiscard]] std::unique_ptr<buffer::Texture2D>& Target(
			bool a_hdr, UINT a_width, UINT a_height);

		std::unique_ptr<buffer::Texture2D> _sdr;
		std::unique_ptr<buffer::Texture2D> _hdr;
	};
}
