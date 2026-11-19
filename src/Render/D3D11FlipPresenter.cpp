#include "D3D11FlipPresenter.h"

#include <atomic>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Annotation.h"
#include "Render/TemporalPipeline.h"

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.d3d11presenter");

		// One-shot trace of the first presents, to place a crash.
		bool TraceFrame(std::atomic_uint& a_count) noexcept
		{
			return a_count.fetch_add(1) < 3;
		}

		constexpr UINT kInheritedFlags =
			DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	}

	HRESULT D3D11FlipPresenter::Create(ID3D11Device& a_device,
		const ChainRequest& a_request, std::unique_ptr<D3D11FlipPresenter>& a_presenter) noexcept
	{
		if (!a_request.window) {
			return E_INVALIDARG;
		}
		// The factory behind the chain's own device, so ReShade's hooks see no proxied device.
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		winrt::com_ptr<IDXGIAdapter> adapter;
		winrt::com_ptr<IDXGIFactory2> factory;
		HRESULT result = a_device.QueryInterface(IID_PPV_ARGS(dxgiDevice.put()));
		if (SUCCEEDED(result)) {
			result = dxgiDevice->GetAdapter(adapter.put());
		}
		if (SUCCEEDED(result)) {
			result = adapter->GetParent(IID_PPV_ARGS(factory.put()));
		}
		if (FAILED(result)) {
			return result;
		}
		BOOL tearing = FALSE;
		if (const auto factory5 = factory.try_as<IDXGIFactory5>()) {
			if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing)))) {
				tearing = FALSE;
			}
		}

		auto presenter = std::make_unique<D3D11FlipPresenter>();
		auto& desc = presenter->_chain.desc;
		desc.Width = a_request.width;
		desc.Height = a_request.height;
		desc.Format = a_request.format;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 2;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		desc.Flags = (a_request.flags & kInheritedFlags) | (tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
		presenter->_tearing = tearing != FALSE;

		winrt::com_ptr<IDXGISwapChain1> chain;
		result = factory->CreateSwapChainForHwnd(&a_device, a_request.window, &desc, nullptr, nullptr, chain.put());
		if (FAILED(result)) {
			return result;
		}
		result = chain->QueryInterface(IID_PPV_ARGS(presenter->_chain.chain.put()));
		if (FAILED(result)) {
			return result;
		}
		result = presenter->RefreshBackBuffer();
		if (FAILED(result)) {
			return result;
		}
		L->info("D3D11 flip presenter created at {}x{} format {}, tearing {}",
			desc.Width, desc.Height, static_cast<unsigned>(desc.Format), presenter->_tearing ? "on" : "off");
		a_presenter = std::move(presenter);
		return S_OK;
	}

	HRESULT D3D11FlipPresenter::RefreshBackBuffer() noexcept
	{
		_backBuffer = nullptr;
		return _chain.chain->GetBuffer(0, IID_PPV_ARGS(_backBuffer.put()));
	}

	HRESULT D3D11FlipPresenter::PresentChain(const PresentArgs& a_args) noexcept
	{
		auto args = a_args;
		// Tearing needs interval 0 and a tearing chain; the facade is always windowed.
		args.flags &= ~DXGI_PRESENT_ALLOW_TEARING;
		if (_tearing && !args.syncInterval) {
			args.flags |= DXGI_PRESENT_ALLOW_TEARING;
		}
		return _chain.Present(args);
	}

	HRESULT D3D11FlipPresenter::Present(const PresentFrame& a_frame, const PresentArgs& a_args) noexcept
	{
		static std::atomic_uint traced{ 0 };
		const bool trace = TraceFrame(traced);
		if (trace)
			L->info("Present enter: pending={} hdr={} separated={}", _framePending, a_frame.hdrContainer, a_frame.separated);
		if (!_framePending) {
			if (a_frame.hdrContainer) {
				ApplyHdrColorSpace(a_frame.hdr);
			}
			if (trace)
				L->info("Compose begin");
			const auto* source = a_frame.ComposeDisplay();
			if (trace)
				L->info("Compose end: source={}", static_cast<const void*>(source));
			D3D11_TEXTURE2D_DESC sourceDesc{};
			if (source) {
				source->texture11->GetDesc(&sourceDesc);
			}
			if (source && sourceDesc.Format == _chain.desc.Format) {
				if (trace)
					L->info("CopyResource begin");
				a_frame.context->CopyResource(_backBuffer.get(), source->texture11.get());
				if (trace)
					L->info("CopyResource end");
			} else if (source) {
				CS_LOG_EVERY_MS(L, 5000, spdlog::level::err, "Present source format does not match the swap chain; the frame is not presented.");
			}
			_framePending = true;
		}
		cs::render::annotation::SetMarker("Presentation/D3D11Flip/Present");
		auto& pipeline = render::TemporalPipeline::Get();
		pipeline.BeginPresentAttempt(a_args.flags);
		const HRESULT result = PresentChain(a_args);
		pipeline.EndPresentAttempt(a_args.flags, result);
		if (trace)
			L->info("Chain Present returned {:#010x}", static_cast<std::uint32_t>(result));
		if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
			_framePending = false;
		}
		return result;
	}

	ResizeOutcome D3D11FlipPresenter::Resize(UINT a_width, UINT a_height, UINT a_flags) noexcept
	{
		_framePending = false;
		_backBuffer = nullptr;
		const UINT flags = a_flags | (_chain.desc.Flags & (DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT));
		const HRESULT resizeResult = _chain.chain->ResizeBuffers(_chain.desc.BufferCount, a_width, a_height, _chain.desc.Format, flags);
		if (FAILED(resizeResult)) {
			(void)RefreshBackBuffer();
			return { .result = resizeResult };
		}
		DXGI_SWAP_CHAIN_DESC1 resized{};
		HRESULT result = _chain.chain->GetDesc1(&resized);
		if (SUCCEEDED(result)) {
			result = RefreshBackBuffer();
		}
		if (FAILED(result)) {
			return { .result = result };
		}
		_chain.desc = resized;
		_chain.appliedColorSpace.reset();
		return { .result = resizeResult, .resized = true };
	}

	HRESULT D3D11FlipPresenter::Release() noexcept
	{
		_backBuffer = nullptr;
		_chain.chain = nullptr;
		return S_OK;
	}
}  // namespace cs::render
