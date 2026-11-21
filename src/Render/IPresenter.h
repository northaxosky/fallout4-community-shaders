#pragma once

#include <memory>
#include <optional>

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include "Render/HdrCompose.h"
#include "Render/InteropBridge.h"

namespace cs::render
{
	// What the host composed on the D3D11 device for one present.
	struct PresentFrame
	{
		ID3D11DeviceContext4* context = nullptr;
		hdr::Compose* compose = nullptr;
		// What the game drew; only the interface while the HDR scene is split out.
		SharedD3D11D3D12Texture* proxy = nullptr;
		SharedD3D11D3D12Texture* scene = nullptr;
		SharedD3D11D3D12Texture* output = nullptr;
		// Set when the chain is fp16 scRGB: the signal is converted into it.
		SharedD3D11D3D12Texture* scRgbOutput = nullptr;
		UINT width = 0;
		UINT height = 0;
		hdr::OutputSettings hdr;
		bool hdrContainer = false;
		bool separated = false;

		// The signal on screen; null when the container has nothing to show.
		[[nodiscard]] const SharedD3D11D3D12Texture* ComposeDisplay() const noexcept;
	};

	// The swap chain a presenter creates for the facade.
	struct ChainRequest
	{
		IDXGIAdapter* adapter = nullptr;
		HWND window = nullptr;
		UINT width = 0;
		UINT height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
		// The game's creation flags; only the ones that survive a flip chain apply.
		UINT flags = 0;
	};

	struct PresentArgs
	{
		UINT syncInterval = 0;
		UINT flags = 0;
		const DXGI_PRESENT_PARAMETERS* parameters = nullptr;
		bool present1 = false;
	};

	struct ResizeOutcome
	{
		HRESULT result = E_FAIL;
		// The chain now has the geometry its Desc reports.
		bool resized = false;
	};

	// A real swap chain behind the facade; the host owns the facade surfaces and compose.
	class IPresenter
	{
	public:
		virtual ~IPresenter() = default;

		[[nodiscard]] virtual IDXGISwapChain4* Chain() const noexcept = 0;
		[[nodiscard]] virtual const DXGI_SWAP_CHAIN_DESC1& Desc() const noexcept = 0;
		[[nodiscard]] virtual bool IsReady() const noexcept = 0;
		// Frame textures must be visible to the bridge's D3D12 device.
		[[nodiscard]] virtual bool NeedsBridgeTextures() const noexcept = 0;
		// A present was refused as still drawing; the retry reuses the composed frame.
		[[nodiscard]] virtual bool FramePending() const noexcept = 0;
		// Presents without touching frame contents (DXGI_PRESENT_TEST, unready chain).
		[[nodiscard]] virtual HRESULT PresentChain(const PresentArgs& a_args) noexcept = 0;
		[[nodiscard]] virtual HRESULT Present(const PresentFrame& a_frame, const PresentArgs& a_args) noexcept = 0;
		[[nodiscard]] virtual ResizeOutcome Resize(UINT a_width, UINT a_height, UINT a_flags) noexcept = 0;
		virtual void ApplyHdrColorSpace(const hdr::OutputSettings& a_settings) noexcept = 0;
		// Drops D3D11-side inputs the next frame rewrites.
		virtual void ClearInputs(ID3D11DeviceContext& a_context) noexcept = 0;
	};

	// The chain state both presenters share.
	struct PresenterChain
	{
		winrt::com_ptr<IDXGISwapChain4> chain;
		DXGI_SWAP_CHAIN_DESC1 desc{};
		std::optional<DXGI_COLOR_SPACE_TYPE> appliedColorSpace;

		[[nodiscard]] HRESULT Present(const PresentArgs& a_args) const noexcept;
		// False, and logged, when the source cannot be copied into the back buffer.
		[[nodiscard]] bool AcceptsSource(const SharedD3D11D3D12Texture& a_source) const noexcept;
		// Applied once per change; a rejection is not retried every frame.
		void ApplyHdrColorSpace(const hdr::OutputSettings& a_settings) noexcept;
	};
}  // namespace cs::render
