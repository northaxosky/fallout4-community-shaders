#pragma once

#include "Render/HdrCompose.h"
#include "Utils/CSBuffer.h"

#include <memory>

namespace cs::features::hdr_display
{
	// Encodes the split scene and interface into a texture a screenshot can read.
	class CaptureComposer
	{
	public:
		// R10 PQ for HDR output, R8 gamma 2.2 otherwise; null while the frame is not split.
		[[nodiscard]] const buffer::Texture2D* Compose(
			const render::hdr::OutputSettings& a_settings, bool a_interface);

	private:
		[[nodiscard]] bool EnsureReady();
		[[nodiscard]] std::unique_ptr<buffer::Texture2D>& Target(
			bool a_hdr, UINT a_width, UINT a_height);

		render::hdr::Compose _compose;
		bool _failed = false;
		std::unique_ptr<buffer::Texture2D> _sdr;
		std::unique_ptr<buffer::Texture2D> _hdr;
	};
}
