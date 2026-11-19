#include "IPresenter.h"

#include "Log.h"
#include "LogThrottle.h"

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.presenter");
	}

	const SharedD3D11D3D12Texture* PresentFrame::ComposeDisplay() const noexcept
	{
		if (!hdrContainer) {
			return proxy;
		}
		const auto* source = separated ? scene : proxy;
		if (!compose || !compose->IsReady() || !output || !source || !source->srv11 ||
			!proxy || !proxy->srv11 || !output->uav11) {
			CS_LOG_EVERY_MS(L, 5000, spdlog::level::err, "HDR compose is unavailable; the frame is not presented.");
			return nullptr;
		}
		compose->Dispatch(*context, *source->srv11, separated ? proxy->srv11.get() : nullptr,
			*output->uav11, width, height, hdr);
		if (!scRgbOutput) {
			return output;
		}
		if (!scRgbOutput->uav11 || !output->srv11) {
			CS_LOG_EVERY_MS(L, 5000, spdlog::level::err, "scRGB output is unavailable; the frame is not presented.");
			return nullptr;
		}
		compose->ConvertToScRgb(*context, *output->srv11, *scRgbOutput->uav11, width, height, hdr);
		return scRgbOutput;
	}

	HRESULT PresenterChain::Present(const PresentArgs& a_args) const noexcept
	{
		if (!chain) {
			return DXGI_ERROR_INVALID_CALL;
		}
		return a_args.present1 ? chain->Present1(a_args.syncInterval, a_args.flags, a_args.parameters) : chain->Present(a_args.syncInterval, a_args.flags);
	}

	void PresenterChain::ApplyHdrColorSpace(const hdr::OutputSettings& a_settings) noexcept
	{
		// An fp16 chain carries linear scRGB whether or not HDR is on.
		const auto colorSpace = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : hdr::ColorSpace(a_settings);
		if (!chain || appliedColorSpace == colorSpace) {
			return;
		}
		appliedColorSpace = colorSpace;
		const HRESULT result = chain->SetColorSpace1(colorSpace);
		if (FAILED(result)) {
			L->error("HDR container color space {} was rejected: {:#010x}",
				static_cast<int>(colorSpace), static_cast<std::uint32_t>(result));
			return;
		}
		// Static metadata misbehaves on some monitors, so none is sent.
		chain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
		L->info("HDR container color space set to {}",
			colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 ? "G10 (scRGB)" : a_settings.enabled ? "G2084 (HDR10)" :
																										 "G22 (SDR)");
	}
}  // namespace cs::render
