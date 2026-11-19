#include "PresentationHost.h"
#include "Render/DXGISwapChainFacadeContract.h"

#include <algorithm>
#include <exception>
#include <utility>

#include <REX/TScopeExit.h>

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/D3D11FlipPresenter.h"
#include "Render/InterfaceLayer.h"
#include "Render/TemporalPipeline.h"

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.presentationhost");
	}  // namespace

	PresentationHost::~PresentationHost()
	{
		(void)Rollback();
	}

	HRESULT PresentationHost::Initialize(InteropBridge& a_bridge,
		ID3D11Device* a_device, ID3D11DeviceContext* a_context,
		const DXGI_SWAP_CHAIN_DESC& a_desc, temporal::IFrameGenerationProvider* a_provider,
		TemporalPresentationCallbacks a_callbacks)
	{
		if (_published || !a_device || !a_context || !a_desc.OutputWindow ||
			(a_desc.BufferDesc.Format != DXGI_FORMAT_UNKNOWN &&
				a_desc.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)) {
			return E_INVALIDARG;
		}
		const HRESULT rollbackResult = Rollback();
		if (FAILED(rollbackResult)) {
			return rollbackResult;
		}
		_bridge = &a_bridge;
		features::ReShadeRuntime::Arm();
		_callbacks = std::move(a_callbacks);
		_hdrContainer = static_cast<bool>(_callbacks.queryHdrOutput);
		_hdrDegraded = false;
		_presenterFallback.reset();
		_creationDesc = a_desc;
		_proxyDesc = a_desc;

		HRESULT result = S_OK;
		try {
			DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(_device11.put())));
			DX::ThrowIfFailed(a_context->QueryInterface(IID_PPV_ARGS(_context11.put())));
			_outwardDevice11.copy_from(a_device);
			winrt::com_ptr<IDXGIAdapter> adapter;
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));
			DX::ThrowIfFailed(dxgiDevice->GetAdapter(adapter.put()));
			DXGI_ADAPTER_DESC adapterDesc{};
			_adapterVendorId = SUCCEEDED(adapter->GetDesc(&adapterDesc)) ? adapterDesc.VendorId : 0;

			// Chosen before the format: an R10 chain nothing can fill would present nothing.
			if (_hdrContainer && !_hdrCompose.Initialize(*_device11)) {
				L->error("HDR compose is unavailable; presenting SDR");
				_hdrContainer = false;
				_hdrDegraded = true;
			}

			ChainRequest request{
				.adapter = adapter.get(),
				.window = a_desc.OutputWindow,
				.width = a_desc.BufferDesc.Width,
				.height = a_desc.BufferDesc.Height,
				.format = _hdrContainer ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM,
				.flags = a_desc.Flags
			};
			if (!request.width || !request.height) {
				RECT client{};
				DX::ThrowIfFailed(GetClientRect(a_desc.OutputWindow, &client) ? S_OK : HRESULT_FROM_WIN32(GetLastError()));
				request.width = static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
				request.height = static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));
			}

			if (a_provider) {
				std::unique_ptr<D3D12FrameGenPresenter> presenter;
				const auto created = D3D12FrameGenPresenter::Create(
					a_bridge, *a_provider, _callbacks, request, _hdrContainer, presenter);
				if (created.Succeeded()) {
					_frameGeneration = presenter.get();
					_presenter = std::move(presenter);
				} else {
					_presenterFallback = created;
					L->error("{}", temporal::FormatProviderFailure(
									   "Create the frame-generation presenter", created));
				}
			}
			if (!_presenter) {
				// The one place the chain's device is chosen: the native one, so ReShade's factory proxy never sees the chain.
				const auto chainDevice = features::ReShadeRuntime::NativeDevice(*a_device);
				// ReShade's copy follows the chain format, so HDR here is fp16 scRGB, not R10 HDR10.
				auto d3d11Request = request;
				d3d11Request.format = _hdrContainer ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
				std::unique_ptr<D3D11FlipPresenter> presenter;
				result = D3D11FlipPresenter::Create(*chainDevice, d3d11Request, presenter);
				if (SUCCEEDED(result)) {
					_presenter = std::move(presenter);
					_chainDevice11 = chainDevice;
				}
			}
			if (SUCCEEDED(result)) {
				_proxyDesc = swap_chain_facade::BuildDescription(a_desc, _presenter->Desc());
				Surfaces surfaces;
				result = CreateSurfaces(_presenter->Desc().Width, _presenter->Desc().Height, surfaces);
				if (SUCCEEDED(result)) {
					_proxyBuffer = std::move(surfaces.proxy);
					_hdrOutput = std::move(surfaces.hdrOutput);
					_scRgbOutput = std::move(surfaces.scRgbOutput);
					_hdrScene = std::move(surfaces.hdrScene);
				}
			}
		} catch (const std::exception& e) {
			L->error("Presentation host creation failed: {}", e.what());
			result = E_FAIL;
		} catch (...) {
			L->error("Presentation host creation raised a non-standard exception");
			result = E_FAIL;
		}
		if (FAILED(result)) {
			const HRESULT cleanupResult = Rollback();
			return FAILED(cleanupResult) ? cleanupResult : result;
		}
		if (_hdrContainer && _callbacks.queryHdrOutput) {
			_presenter->ApplyHdrColorSpace(_callbacks.queryHdrOutput());
		}

		_proxy.attach(new DXGISwapChainProxy(*this, *_presenter->Chain()));
		_published = true;
		features::ReShadeRuntime::GuardWindow(a_desc.OutputWindow);
		if (_frameGeneration && _bridge->GetCaptureDevice() && _callbacks.bindD3D12CaptureTarget) {
			_captureDevice12.copy_from(_bridge->GetCaptureDevice());
			_callbacks.bindD3D12CaptureTarget(_captureDevice12.get(), a_desc.OutputWindow);
			_captureBound = true;
		}
		_presentationBound = true;
		NotifyPresentationBinding();
		ClearSurfaces();
		if (_frameGeneration) {
			_frameGeneration->ClearInputs(*_context11);
		}
		const auto& chain = _presenter->Desc();
		L->info("Published the D3D11-facing facade at {}x{} {} on the {} presenter",
			chain.Width, chain.Height,
			_hdrContainer ? "R10G10B10A2_UNORM (HDR container)" : "R8G8B8A8_UNORM",
			_frameGeneration ? "D3D12 frame-generation" : "D3D11 flip");
		return S_OK;
	}

	HRESULT PresentationHost::Rollback() noexcept
	{
		if (_captureBound && _callbacks.unbindD3D12CaptureTarget) {
			_callbacks.unbindD3D12CaptureTarget(_captureDevice12.get());
		}
		_captureBound = false;
		_captureDevice12 = nullptr;
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		if (_proxy) {
			_proxy->DetachOwner();
			_proxy = nullptr;
		}
		_published = false;
		ReleaseReShade();
		_hdrCompose.Reset();
		const bool quarantined = _bridge && _bridge->IsQuarantined() && _frameGeneration;
		if (quarantined) {
			// The GPU may still read these; the presenter retains its own.
			InteropBridge::Retain(_proxyBuffer);
			InteropBridge::Retain(_hdrOutput);
			InteropBridge::Retain(_hdrScene);
		}
		_frameGeneration = nullptr;
		_presenter.reset();
		_proxyBuffer.reset();
		_hdrOutput.reset();
		_hdrScene.reset();
		_scRgbOutput.reset();
		if (std::exchange(_presentationBound, false) && _callbacks.unbindPresentation) {
			_callbacks.unbindPresentation();
		}
		_callbacks = {};
		_presenterFallback.reset();
		_context11 = nullptr;
		_chainDevice11 = nullptr;
		_outwardDevice11 = nullptr;
		_device11 = nullptr;
		_bridge = nullptr;
		_hdrContainer = false;
		_hdrDegraded = false;
		return quarantined ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
	}

	HRESULT PresentationHost::CreateSurfaces(UINT a_width, UINT a_height, Surfaces& a_surfaces) const
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		// The game still sees R8; an HDR scene needs the headroom of fp16.
		desc.Format = _hdrContainer ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = swap_chain_facade::BackBufferBindFlags(_proxyDesc.BufferUsage);
		auto* device12 = _presenter->NeedsBridgeTextures() ? _bridge->GetD3D12Device() : nullptr;
		Surfaces surfaces;
		surfaces.proxy = SharedD3D11D3D12Texture::Create(
			_device11.get(), device12, desc, "Upscaling/FrameGenerationProxy");
		if (_hdrContainer) {
			surfaces.hdrScene = SharedD3D11D3D12Texture::Create(
				_device11.get(), device12, desc, "HDRDisplay/Scene");
			desc.Format = _presenter->Desc().Format;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			surfaces.hdrOutput = SharedD3D11D3D12Texture::Create(
				_device11.get(), device12, desc, "Present/HdrOutput");
			if (IsScRgbChain()) {
				surfaces.scRgbOutput = SharedD3D11D3D12Texture::Create(
					_device11.get(), device12, desc, "Present/ScRgbOutput");
			}
		}
		if (!surfaces.Complete(_hdrContainer, IsScRgbChain())) {
			return E_OUTOFMEMORY;
		}
		a_surfaces = std::move(surfaces);
		return S_OK;
	}

	void PresentationHost::ClearSurfaces() noexcept
	{
		if (!_context11) {
			return;
		}
		const float clear[4]{};
		if (_proxyBuffer && _proxyBuffer->rtv11) {
			_context11->ClearRenderTargetView(_proxyBuffer->rtv11.get(), clear);
		}
		if (_hdrScene && _hdrScene->rtv11) {
			_context11->ClearRenderTargetView(_hdrScene->rtv11.get(), clear);
		}
	}

	IDXGISwapChain* PresentationHost::GetProxy() const noexcept
	{
		return _proxy.get();
	}

	IDXGISwapChain* PresentationHost::AcquireProxy() const noexcept
	{
		auto* proxy = _proxy.get();
		if (proxy) {
			proxy->AddRef();
		}
		return proxy;
	}

	bool PresentationHost::Owns(IDXGISwapChain* a_swapChain) const noexcept
	{
		return _proxy && a_swapChain == _proxy.get();
	}

	bool PresentationHost::IsReady() const noexcept
	{
		return _published && _presenter && _presenter->IsReady() && _context11 &&
		       _proxyBuffer && (!_hdrContainer || (_hdrOutput && _hdrScene)) && (!IsScRgbChain() || _scRgbOutput);
	}

	bool PresentationHost::IsFrameGenerationReady() const noexcept
	{
		return IsReady() && _frameGeneration && _frameGeneration->IsFrameGenerationReady();
	}

	bool PresentationHost::IsHdrContainer() const noexcept
	{
		return _hdrContainer && IsReady();
	}

	temporal::IFrameGenerationProvider* PresentationHost::GetPresentationProvider() const noexcept
	{
		return _frameGeneration ? &_frameGeneration->Provider() : nullptr;
	}

	UINT PresentationHost::GetWidth() const noexcept { return _presenter ? _presenter->Desc().Width : 0; }

	UINT PresentationHost::GetHeight() const noexcept { return _presenter ? _presenter->Desc().Height : 0; }

	UINT PresentationHost::GetFrameSlot() const noexcept { return _frameGeneration ? _frameGeneration->GetFrameSlot() : 0; }

	SharedD3D11D3D12Texture* PresentationHost::GetHudlessTexture() const noexcept
	{
		return _frameGeneration ? _frameGeneration->GetHudlessTexture() : nullptr;
	}

	SharedD3D11D3D12Texture* PresentationHost::GetDepthTexture() const noexcept
	{
		return _frameGeneration ? _frameGeneration->GetDepthTexture() : nullptr;
	}

	SharedD3D11D3D12Texture* PresentationHost::GetMotionTexture() const noexcept
	{
		return _frameGeneration ? _frameGeneration->GetMotionTexture() : nullptr;
	}

	temporal::PresentInputRetirementDiagnostics
	PresentationHost::GetInputRetirementDiagnostics() const noexcept
	{
		return _frameGeneration ? _frameGeneration->GetInputRetirementDiagnostics() : temporal::PresentInputRetirementDiagnostics{};
	}

	void PresentationHost::SetFrameGenerationInputsReady(bool a_ready) noexcept
	{
		if (_frameGeneration) {
			_frameGeneration->SetFrameGenerationInputsReady(a_ready);
		}
	}

	void PresentationHost::SetOutwardD3D11Device(ID3D11Device* a_device) noexcept
	{
		if (a_device) {
			_outwardDevice11.copy_from(a_device);
		}
	}

	void PresentationHost::DisableFrameGeneration(const char* a_reason) noexcept
	{
		if (_frameGeneration) {
			_frameGeneration->DisableFrameGeneration(a_reason);
		}
	}

	bool PresentationHost::AcquireFrameGenerationInputWrite() noexcept
	{
		return _frameGeneration && _frameGeneration->AcquireFrameGenerationInputWrite();
	}

	void PresentationHost::NotifyPresentationBinding() noexcept
	{
		if (_presentationBound && _presenter && _callbacks.bindPresentation) {
			const bool d3d12 = _frameGeneration != nullptr;
			_callbacks.bindPresentation({ .device = d3d12 ? static_cast<IUnknown*>(_captureDevice12.get()) : _chainDevice11.get(),
				.api = d3d12 ? temporal::DeviceApi::kD3D12 : temporal::DeviceApi::kD3D11,
				.adapterVendorId = _adapterVendorId,
				.swapChain = _presenter->Chain(),
				.frameGenerationPresentation = _frameGeneration != nullptr });
		}
	}

	std::optional<features::ScRgbProxy> PresentationHost::ScRgbFor(
		const SharedD3D11D3D12Texture* a_color, float a_whiteNits, bool a_enabled) const noexcept
	{
		if (!_hdrContainer || !a_enabled || !a_color)
			return std::nullopt;
		return features::ScRgbProxy{ .srv = a_color->srv11.get(), .rtv = a_color->rtv11.get(), .whiteNits = a_whiteNits };
	}

	hdr::SceneTarget PresentationHost::GetHdrSceneTarget() const noexcept
	{
		if (!_hdrContainer || !_hdrScene)
			return {};
		return { _hdrScene->texture11.get(), _hdrScene->srv11.get(), _hdrScene->rtv11.get() };
	}

	bool PresentationHost::ComposeToTarget(ID3D11ShaderResourceView& a_scene, ID3D11ShaderResourceView* a_interface,
		ID3D11UnorderedAccessView& a_output, UINT a_width, UINT a_height, const hdr::OutputSettings& a_settings) noexcept
	{
		if (!_hdrContainer || !_hdrCompose.IsReady() || !_context11)
			return false;
		_hdrCompose.Dispatch(*_context11, a_scene, a_interface, a_output, a_width, a_height, a_settings);
		return true;
	}

	bool PresentationHost::PrepareReShade() noexcept
	{
		return features::ReShadeRuntime::IsInstalled() && IsReady() && _outwardDevice11 &&
		       _reshade.Ensure(*this, *Inner(), *_outwardDevice11,
				   _presenter->Desc().Width, _presenter->Desc().Height, _hdrContainer,
				   _frameGeneration ? features::ReShadeRuntime::Attach::kHosted : features::ReShadeRuntime::Attach::kAdopt);
	}

	void PresentationHost::ReleaseReShade() noexcept
	{
		if (_bridge && _bridge->IsQuarantined() && _frameGeneration) {
			_reshade.Abandon();
		} else {
			_reshade.Destroy();
		}
	}

	bool PresentationHost::ReShadeEffectsEnabled() noexcept
	{
		return PrepareReShade() && _reshade.EffectsEnabled();
	}

	void PresentationHost::RenderReShadeEffects(ID3D11DeviceContext& a_context,
		ID3D11Resource& a_color, ID3D11ShaderResourceView* a_depth, float a_hdrWhiteNits) noexcept
	{
		// Only the proxy, or the scene standing in for it, is what the game presents.
		const SharedD3D11D3D12Texture* color = nullptr;
		if (_proxyBuffer && &a_color == static_cast<ID3D11Resource*>(_proxyBuffer->texture11.get()))
			color = _proxyBuffer.get();
		else if (_hdrScene && &a_color == static_cast<ID3D11Resource*>(_hdrScene->texture11.get()))
			color = _hdrScene.get();
		if (color && PrepareReShade()) {
			const auto scRgb = ScRgbFor(color, a_hdrWhiteNits, a_hdrWhiteNits > 0.0f);
			_reshade.RenderEffects(a_context, a_color, a_depth, scRgb ? &*scRgb : nullptr);
		}
	}

	HRESULT PresentationHost::Present(UINT a_syncInterval, UINT a_flags) noexcept
	{
		return PresentInternal(a_syncInterval, a_flags, nullptr, false);
	}

	HRESULT
	PresentationHost::Present1(UINT a_syncInterval, UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters) noexcept
	{
		return PresentInternal(a_syncInterval, a_flags, a_parameters, true);
	}

	PresentFrame PresentationHost::BeginFrame() noexcept
	{
		PresentFrame frame{
			.context = _context11.get(),
			.compose = &_hdrCompose,
			.proxy = _proxyBuffer.get(),
			.scene = _hdrScene.get(),
			.output = _hdrOutput.get(),
			.scRgbOutput = _scRgbOutput.get(),
			.width = _presenter->Desc().Width,
			.height = _presenter->Desc().Height,
			.hdr = _hdrContainer && _callbacks.queryHdrOutput ? _callbacks.queryHdrOutput() : hdr::OutputSettings{},
			.hdrContainer = _hdrContainer,
			// Consumed once per present; the scene texture holds this frame's scene.
			.separated = _hdrContainer && InterfaceLayer::Get().ConsumeSeparated() && _hdrScene
		};
		if (PrepareReShade()) {
			// A separated frame's proxy holds only the interface, composed after the effects.
			const auto* color = frame.separated ? frame.scene : frame.proxy;
			const auto scRgb = ScRgbFor(color, frame.hdr.paperWhite, frame.hdr.enabled);
			_reshade.Present(*_context11, *color->texture11, scRgb ? &*scRgb : nullptr);
		}
		return frame;
	}

	HRESULT PresentationHost::PresentInternal(UINT a_syncInterval, UINT a_flags,
		const DXGI_PRESENT_PARAMETERS* a_parameters, bool a_usePresent1) noexcept
	{
		auto& pipeline = render::TemporalPipeline::Get();
		const PresentArgs args{ .syncInterval = a_syncInterval, .flags = a_flags, .parameters = a_parameters, .present1 = a_usePresent1 };
		if (a_flags & DXGI_PRESENT_TEST) {
			pipeline.BeginPresentAttempt(a_flags);
			const HRESULT result = _presenter ? _presenter->PresentChain(args) : DXGI_ERROR_INVALID_CALL;
			pipeline.EndPresentAttempt(a_flags, result);
			if (_frameGeneration) {
				_frameGeneration->RecordTestPresent(a_flags, result);
			}
			return result;
		}
		const REX::TScopeExit framed{ [this]() noexcept {
			// A D3D11 presenter's own chain is already the boundary RenderDoc follows.
			if (_captureBound && _callbacks.gameFramePresented) {
				_callbacks.gameFramePresented();
			}
		} };
		HRESULT result = DXGI_ERROR_INVALID_CALL;
		if (!IsReady()) {
			if (_presenter) {
				pipeline.BeginPresentAttempt(a_flags);
				result = _presenter->PresentChain(args);
				pipeline.EndPresentAttempt(a_flags, result);
			}
		} else {
			try {
				const PresentFrame frame = _presenter->FramePending() ? PresentFrame{} : BeginFrame();
				result = _presenter->Present(frame, args);
				if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
					ClearSurfaces();
				}
			} catch (const std::exception& e) {
				L->error("Presentation failed: {}", e.what());
				result = E_FAIL;
			} catch (...) {
				L->error("Presentation raised a non-standard exception");
				result = E_FAIL;
			}
		}
		if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
			// Only the frame-generation presenter's chain reaches Streamline's own present hooks.
			if (_bridge && !_frameGeneration) {
				_bridge->NotifyHostFramePresented(a_flags);
			}
			if (_callbacks.clearCapture) {
				_callbacks.clearCapture();
			}
		}
		return result;
	}

	HRESULT PresentationHost::GetBuffer(UINT a_buffer, REFIID a_iid,
		void** a_surface) noexcept
	{
		if (!a_surface) {
			return E_POINTER;
		}
		*a_surface = nullptr;
		if (a_buffer != 0 || !_proxyBuffer || !_proxyBuffer->texture11) {
			return DXGI_ERROR_INVALID_CALL;
		}
		return _proxyBuffer->texture11->QueryInterface(a_iid, a_surface);
	}

	HRESULT PresentationHost::GetDevice(REFIID a_iid, void** a_device) noexcept
	{
		if (!a_device) {
			return E_POINTER;
		}
		*a_device = nullptr;
		return _outwardDevice11 ? _outwardDevice11->QueryInterface(a_iid, a_device) : E_NOINTERFACE;
	}

	HRESULT PresentationHost::SetFullscreenState(BOOL a_fullscreen,
		IDXGIOutput*) noexcept
	{
		if (!Inner()) {
			return DXGI_ERROR_INVALID_CALL;
		}
		// Exclusive is unsupported; Renderer::Begin retries until it reads it back.
		_proxyDesc.Windowed = !a_fullscreen;
		return S_OK;
	}

	HRESULT PresentationHost::GetFullscreenState(BOOL* a_fullscreen,
		IDXGIOutput** a_target) noexcept
	{
		if (!a_fullscreen) {
			if (a_target) {
				*a_target = nullptr;
			}
			return E_POINTER;
		}
		*a_fullscreen = !_proxyDesc.Windowed;
		if (a_target) {
			*a_target = nullptr;
			if (*a_fullscreen && FAILED(GetContainingOutput(a_target))) {
				*a_target = nullptr;
			}
		}
		return S_OK;
	}

	HRESULT PresentationHost::GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc = _proxyDesc;
		return S_OK;
	}

	HRESULT PresentationHost::GetDesc1(DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc = swap_chain_facade::BuildDescription1(_proxyDesc);
		return S_OK;
	}

	HRESULT PresentationHost::GetFullscreenDesc(
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept
	{
		if (!a_desc) {
			return E_POINTER;
		}
		*a_desc = swap_chain_facade::BuildFullscreenDescription(_proxyDesc);
		return S_OK;
	}

	HRESULT PresentationHost::GetHwnd(HWND* a_window) noexcept
	{
		if (!a_window) {
			return E_POINTER;
		}
		*a_window = _proxyDesc.OutputWindow;
		return S_OK;
	}

	UINT PresentationHost::GetCurrentBackBufferIndex() noexcept { return 0; }

	HRESULT
	PresentationHost::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE a_colorSpace,
		UINT* a_support) noexcept
	{
		if (!a_support) {
			return E_POINTER;
		}
		*a_support = 0;
		if (a_colorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
			return S_OK;
		}
		return Inner() ? Inner()->CheckColorSpaceSupport(a_colorSpace, a_support) : DXGI_ERROR_INVALID_CALL;
	}

	HRESULT
	PresentationHost::SetColorSpace1(DXGI_COLOR_SPACE_TYPE a_colorSpace) noexcept
	{
		if (a_colorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		return Inner() ? Inner()->SetColorSpace1(a_colorSpace) : DXGI_ERROR_INVALID_CALL;
	}

	HRESULT PresentationHost::SetHDRMetaData(DXGI_HDR_METADATA_TYPE a_type,
		UINT a_size, void* a_metadata) noexcept
	{
		if (a_type != DXGI_HDR_METADATA_TYPE_NONE) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		if (a_size || a_metadata) {
			return E_INVALIDARG;
		}
		return Inner() ? Inner()->SetHDRMetaData(a_type, 0, nullptr) : DXGI_ERROR_INVALID_CALL;
	}

	HRESULT PresentationHost::ResizeTarget(const DXGI_MODE_DESC* a_target) noexcept
	{
		if (!a_target) {
			return E_INVALIDARG;
		}
		if (!Inner()) {
			return DXGI_ERROR_INVALID_CALL;
		}
		const HRESULT result = Inner()->ResizeTarget(a_target);
		if (SUCCEEDED(result)) {
			_proxyDesc.BufferDesc = *a_target;
			_creationDesc.BufferDesc = *a_target;
		}
		return result;
	}

	HRESULT PresentationHost::ResizeBuffers(UINT a_bufferCount, UINT a_width,
		UINT a_height, DXGI_FORMAT a_format, UINT a_flags) noexcept
	{
		if (_callbacks.clearCapture) {
			_callbacks.clearCapture();
		}
		try {
			return ResizeBuffersImpl(a_bufferCount, a_width, a_height, a_format, a_flags);
		} catch (const std::exception& e) {
			L->error("Resize failed: {}", e.what());
			DisableFrameGeneration(e.what());
			return E_FAIL;
		} catch (...) {
			L->error("Resize raised a non-standard exception");
			DisableFrameGeneration("unhandled resize failure");
			return E_FAIL;
		}
	}

	HRESULT
	PresentationHost::ResizeBuffers1(UINT a_bufferCount, UINT a_width, UINT a_height,
		DXGI_FORMAT a_format, UINT a_flags,
		const UINT* a_creationNodeMask,
		IUnknown* const* a_presentQueue) noexcept
	{
		if (a_creationNodeMask || a_presentQueue) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		return ResizeBuffers(a_bufferCount, a_width, a_height, a_format, a_flags);
	}

	HRESULT PresentationHost::ResizeBuffersImpl(UINT a_bufferCount, UINT a_width,
		UINT a_height, DXGI_FORMAT a_format, UINT a_flags)
	{
		if (!Inner()) {
			return DXGI_ERROR_INVALID_CALL;
		}
		if (!swap_chain_facade::SupportsResizeBufferCount(a_bufferCount, _proxyDesc)) {
			return DXGI_ERROR_UNSUPPORTED;
		}
		if (a_format != DXGI_FORMAT_UNKNOWN &&
			a_format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			return DXGI_ERROR_UNSUPPORTED;
		}

		UINT targetWidth = a_width;
		UINT targetHeight = a_height;
		if (!targetWidth || !targetHeight) {
			RECT client{};
			if (!GetClientRect(_proxyDesc.OutputWindow, &client)) {
				return HRESULT_FROM_WIN32(GetLastError());
			}
			if (!targetWidth) {
				targetWidth =
					static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
			}
			if (!targetHeight) {
				targetHeight =
					static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));
			}
		}

		const UINT oldWidth = _presenter->Desc().Width;
		const UINT oldHeight = _presenter->Desc().Height;
		Surfaces pending;
		if (targetWidth != oldWidth || targetHeight != oldHeight) {
			if (_bridge && !_frameGeneration) {
				_bridge->NotifyHostResize();
			}
			const HRESULT surfaceResult = CreateSurfaces(targetWidth, targetHeight, pending);
			if (FAILED(surfaceResult)) {
				return surfaceResult;
			}
		}

		const auto outcome = _presenter->Resize(targetWidth, targetHeight, a_flags);
		if (!outcome.resized) {
			return outcome.result;
		}
		const auto& resized = _presenter->Desc();
		const bool sizeChanged = resized.Width != oldWidth || resized.Height != oldHeight;
		if (sizeChanged) {
			if (!pending.Complete(_hdrContainer, IsScRgbChain()) || targetWidth != resized.Width || targetHeight != resized.Height) {
				const HRESULT surfaceResult = CreateSurfaces(resized.Width, resized.Height, pending);
				if (FAILED(surfaceResult)) {
					_published = false;
					DisableFrameGeneration("resized display resources were unavailable");
					return surfaceResult;
				}
			}
			// ReShade's copy and runtime are sized at creation.
			ReleaseReShade();
			_proxyBuffer = std::move(pending.proxy);
			_hdrOutput = std::move(pending.hdrOutput);
			_scRgbOutput = std::move(pending.scRgbOutput);
			_hdrScene = std::move(pending.hdrScene);
		}
		_proxyDesc.BufferDesc.Width = resized.Width;
		_proxyDesc.BufferDesc.Height = resized.Height;
		_creationDesc.BufferDesc.Width = resized.Width;
		_creationDesc.BufferDesc.Height = resized.Height;
		_creationDesc.BufferDesc.Format = _proxyDesc.BufferDesc.Format;
		if (FAILED(outcome.result)) {
			return outcome.result;
		}
		ClearSurfaces();
		if (_frameGeneration) {
			_frameGeneration->ClearInputs(*_context11);
		}
		render::TemporalPipeline::Get().AdvanceDisplayGeneration(resized.Width, resized.Height);
		return outcome.result;
	}

	HRESULT PresentationHost::SetPrivateData(REFGUID a_name, UINT a_size,
		const void* a_data) noexcept
	{
		return Inner() ? Inner()->SetPrivateData(a_name, a_size, a_data) : E_FAIL;
	}

	HRESULT
	PresentationHost::SetPrivateDataInterface(REFGUID a_name,
		const IUnknown* a_unknown) noexcept
	{
		return Inner() ? Inner()->SetPrivateDataInterface(a_name, a_unknown) : E_FAIL;
	}

	HRESULT PresentationHost::GetPrivateData(REFGUID a_name, UINT* a_size,
		void* a_data) noexcept
	{
		return Inner() ? Inner()->GetPrivateData(a_name, a_size, a_data) : E_FAIL;
	}

	HRESULT PresentationHost::GetParent(REFIID a_iid, void** a_parent) noexcept
	{
		return Inner() ? Inner()->GetParent(a_iid, a_parent) : E_FAIL;
	}

	HRESULT PresentationHost::GetContainingOutput(IDXGIOutput** a_output) noexcept
	{
		return Inner() ? Inner()->GetContainingOutput(a_output) : E_FAIL;
	}

	HRESULT
	PresentationHost::GetFrameStatistics(DXGI_FRAME_STATISTICS* a_stats) noexcept
	{
		return Inner() ? Inner()->GetFrameStatistics(a_stats) : E_FAIL;
	}

	HRESULT PresentationHost::GetLastPresentCount(UINT* a_count) noexcept
	{
		return Inner() ? Inner()->GetLastPresentCount(a_count) : E_FAIL;
	}
}  // namespace cs::render
