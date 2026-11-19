#pragma once

#include "Render/IPresenter.h"

namespace cs::render
{
	// A flip-model chain on the engine's own D3D11 device: SDR is R8, and HDR is the
	// R10 container the compose pass encodes into.
	class D3D11FlipPresenter final : public IPresenter
	{
	public:
		[[nodiscard]] static HRESULT Create(ID3D11Device& a_device,
			const ChainRequest& a_request, std::unique_ptr<D3D11FlipPresenter>& a_presenter) noexcept;

		[[nodiscard]] IDXGISwapChain4* Chain() const noexcept override { return _chain.chain.get(); }
		[[nodiscard]] const DXGI_SWAP_CHAIN_DESC1& Desc() const noexcept override { return _chain.desc; }
		[[nodiscard]] bool IsReady() const noexcept override { return _chain.chain && _backBuffer; }
		[[nodiscard]] bool NeedsBridgeTextures() const noexcept override { return false; }
		[[nodiscard]] bool FramePending() const noexcept override { return _framePending; }
		[[nodiscard]] HRESULT PresentChain(const PresentArgs& a_args) noexcept override;
		[[nodiscard]] HRESULT Present(const PresentFrame& a_frame, const PresentArgs& a_args) noexcept override;
		[[nodiscard]] ResizeOutcome Resize(UINT a_width, UINT a_height, UINT a_flags) noexcept override;
		void ApplyHdrColorSpace(const hdr::OutputSettings& a_settings) noexcept override { _chain.ApplyHdrColorSpace(a_settings); }
		void ClearInputs(ID3D11DeviceContext&) noexcept override {}
		[[nodiscard]] HRESULT Release() noexcept override;

	private:
		HRESULT RefreshBackBuffer() noexcept;

		PresenterChain _chain;
		winrt::com_ptr<ID3D11Texture2D> _backBuffer;
		bool _tearing = false;
		bool _framePending = false;
	};
}  // namespace cs::render
