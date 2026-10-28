#include "ReShadeRuntime.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/RendererContext.h"
#include "Render/ScopedContextState.h"
#include "Utils/ModuleInfo.h"

// Last: this header must not see imgui.h.
#include <reshade.hpp>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.reshade");

		using ::reshade::api::effect_runtime;

		// The reload event fires on ReShade's loader thread.
		std::atomic<const effect_runtime*> g_runtime{};
		std::atomic_bool g_effectsReloaded{ true };

		void OnReloadedEffects(effect_runtime* a_runtime)
		{
			if (a_runtime == g_runtime.load(std::memory_order_acquire)) {
				g_effectsReloaded.store(true, std::memory_order_release);
			}
		}

		template <class T>
		::reshade::api::resource_view ViewHandle(T* a_view) noexcept
		{
			return { reinterpret_cast<std::uint64_t>(a_view) };
		}

		bool FindReShade() noexcept
		{
			const bool found = ::reshade::internal::get_reshade_module_handle() != nullptr;
			L->debug("ReShade module {}", found ? "found" : "not loaded");
			return found;
		}

		// Presents the typeless copy to ReShade as swap chain buffer 0.
		class BackBufferOwner final : public IDXGISwapChainProxyOwner
		{
		public:
			BackBufferOwner(IDXGISwapChainProxyOwner& a_host, ID3D11Texture2D& a_buffer) noexcept :
				_host(a_host)
			{
				_buffer.copy_from(&a_buffer);
			}

			HRESULT Present(UINT, UINT) noexcept override
			{
				return DXGI_ERROR_INVALID_CALL;
			}
			HRESULT Present1(UINT, UINT, const DXGI_PRESENT_PARAMETERS*) noexcept override
			{
				return DXGI_ERROR_INVALID_CALL;
			}
			HRESULT GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface) noexcept override
			{
				return a_buffer == 0 ? _buffer->QueryInterface(a_iid, a_surface) : DXGI_ERROR_INVALID_CALL;
			}
			HRESULT GetDevice(REFIID a_iid, void** a_device) noexcept override
			{
				return _host.GetDevice(a_iid, a_device);
			}
			HRESULT SetFullscreenState(BOOL, IDXGIOutput*) noexcept override
			{
				return DXGI_ERROR_INVALID_CALL;
			}
			HRESULT GetFullscreenState(BOOL* a_fullscreen, IDXGIOutput** a_target) noexcept override
			{
				return _host.GetFullscreenState(a_fullscreen, a_target);
			}
			HRESULT GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) noexcept override
			{
				return _host.GetDesc(a_desc);
			}
			HRESULT ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT, UINT) noexcept override
			{
				return DXGI_ERROR_INVALID_CALL;
			}
			HRESULT ResizeTarget(const DXGI_MODE_DESC*) noexcept override
			{
				return DXGI_ERROR_INVALID_CALL;
			}
			HRESULT GetDesc1(DXGI_SWAP_CHAIN_DESC1* a_desc) noexcept override
			{
				return _host.GetDesc1(a_desc);
			}
			HRESULT GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_desc) noexcept override
			{
				return _host.GetFullscreenDesc(a_desc);
			}
			HRESULT GetHwnd(HWND* a_window) noexcept override
			{
				return _host.GetHwnd(a_window);
			}
			UINT GetCurrentBackBufferIndex() noexcept override
			{
				return 0;
			}
			HRESULT CheckColorSpaceSupport(
				DXGI_COLOR_SPACE_TYPE a_colorSpace, UINT* a_support) noexcept override
			{
				return _host.CheckColorSpaceSupport(a_colorSpace, a_support);
			}
			HRESULT SetColorSpace1(DXGI_COLOR_SPACE_TYPE) noexcept override
			{
				return DXGI_ERROR_INVALID_CALL;
			}
			HRESULT ResizeBuffers1(UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*,
				IUnknown* const*) noexcept override
			{
				return DXGI_ERROR_INVALID_CALL;
			}
			HRESULT SetHDRMetaData(DXGI_HDR_METADATA_TYPE, UINT, void*) noexcept override
			{
				return DXGI_ERROR_INVALID_CALL;
			}

		private:
			IDXGISwapChainProxyOwner& _host;
			winrt::com_ptr<ID3D11Texture2D> _buffer;
		};
	}

	struct ReShadeRuntime::Impl
	{
		std::unique_ptr<BackBufferOwner> owner;
		winrt::com_ptr<DXGISwapChainProxy> proxy;
		winrt::com_ptr<ID3D11Texture2D> color;
		winrt::com_ptr<ID3D11RenderTargetView> rtv;
		winrt::com_ptr<ID3D11RenderTargetView> rtvSrgb;
		winrt::com_ptr<ID3DDeviceContextState> contextState;
		effect_runtime* runtime = nullptr;
		ID3D11ShaderResourceView* boundDepth = nullptr;
		bool addonRegistered = false;

		~Impl()
		{
			if (runtime) {
				g_runtime.store(nullptr, std::memory_order_release);
				::reshade::destroy_effect_runtime(runtime);
			}
			if (addonRegistered) {
				::reshade::unregister_event<::reshade::addon_event::reshade_reloaded_effects>(&OnReloadedEffects);
				::reshade::unregister_addon(cs::util::ModuleFromAddress(reinterpret_cast<const void*>(&OnReloadedEffects)));
			}
			if (proxy) {
				proxy->DetachOwner();
			}
		}

		void Create(IDXGISwapChainProxyOwner& a_host, IDXGISwapChain4& a_inner,
			ID3D11Device& a_device, UINT a_width, UINT a_height)
		{
			winrt::com_ptr<ID3D11Device> device;
			DX::ThrowIfFailed(a_device.QueryInterface(IID_PPV_ARGS(device.put())));
			winrt::com_ptr<ID3D11Device1> device1;
			DX::ThrowIfFailed(a_device.QueryInterface(IID_PPV_ARGS(device1.put())));

			// ReShade builds linear and sRGB views, which the shared UNORM proxy can't.
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, color.put()));
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			DX::ThrowIfFailed(device->CreateRenderTargetView(color.get(), &rtvDesc, rtv.put()));
			rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			DX::ThrowIfFailed(device->CreateRenderTargetView(color.get(), &rtvDesc, rtvSrgb.put()));
			cs::render::annotation::SetName(color.get(), "Upscaling/ReShade.Color");
			cs::render::annotation::SetName(rtv.get(), "Upscaling/ReShade.Color.RTV");
			cs::render::annotation::SetName(rtvSrgb.get(), "Upscaling/ReShade.Color.RTV_sRGB");

			// ReShade rebinds state freely; a private state keeps the engine's intact.
			const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
			D3D_FEATURE_LEVEL selected{};
			DX::ThrowIfFailed(device1->CreateDeviceContextState(
				0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &selected,
				contextState.put()));

			owner = std::make_unique<BackBufferOwner>(a_host, *color);
			proxy.attach(new DXGISwapChainProxy(*owner, a_inner));

			// Events need an add-on identity; without it, rebind every frame.
			addonRegistered = ::reshade::register_addon(cs::util::ModuleFromAddress(reinterpret_cast<const void*>(&OnReloadedEffects)));
			if (addonRegistered) {
				::reshade::register_event<::reshade::addon_event::reshade_reloaded_effects>(&OnReloadedEffects);
			}
			g_effectsReloaded.store(true, std::memory_order_release);

			// A relative path resolves against ReShade's base path, like its own runtime.
			if (!::reshade::create_effect_runtime(::reshade::api::device_api::d3d11, device.get(),
					nullptr, proxy.get(), "ReShade.ini", &runtime)) {
				runtime = nullptr;
				DX::ThrowIfFailed(E_FAIL);
			}
			g_runtime.store(runtime, std::memory_order_release);
		}

		// Reloading effects drops the DEPTH binding and resets bufready_depth uniforms.
		void BindDepth(ID3D11ShaderResourceView* a_depth)
		{
			const bool reloaded = g_effectsReloaded.exchange(false, std::memory_order_acq_rel) || !addonRegistered;
			if (!reloaded && a_depth == boundDepth) {
				return;
			}
			boundDepth = a_depth;
			const auto handle = ViewHandle(a_depth);
			runtime->update_texture_bindings("DEPTH", handle, handle);
			const bool ready = a_depth != nullptr;
			runtime->enumerate_uniform_variables(nullptr, [ready](effect_runtime* a_runtime, ::reshade::api::effect_uniform_variable a_variable) {
				char source[32];
				if (a_runtime->get_annotation_string_from_uniform_variable(a_variable, "source", source) &&
					std::strcmp(source, "bufready_depth") == 0) {
					a_runtime->set_uniform_value_bool(a_variable, ready);
				}
			});
		}
	};

	ReShadeRuntime::ReShadeRuntime() noexcept = default;

	ReShadeRuntime::~ReShadeRuntime() = default;

	bool ReShadeRuntime::IsInstalled() noexcept
	{
		static const bool installed = FindReShade();
		return installed;
	}

	bool ReShadeRuntime::Ensure(IDXGISwapChainProxyOwner& a_host, IDXGISwapChain4& a_inner,
		ID3D11Device& a_device, UINT a_width, UINT a_height) noexcept
	{
		if (_attempted) {
			return _impl != nullptr;
		}
		_attempted = true;
		try {
			_impl = std::make_unique<Impl>();
			_impl->Create(a_host, a_inner, a_device, a_width, a_height);
			L->info("ReShade effect runtime created at {}x{}", a_width, a_height);
		} catch (const std::exception& e) {
			L->warn("ReShade effect runtime unavailable: {}", e.what());
			_impl.reset();
		} catch (...) {
			L->warn("ReShade effect runtime unavailable");
			_impl.reset();
		}
		return _impl != nullptr;
	}

	void ReShadeRuntime::Abandon() noexcept
	{
		(void)_impl.release();
	}

	void ReShadeRuntime::Destroy() noexcept
	{
		_impl.reset();
		_attempted = false;
	}

	void ReShadeRuntime::ReplaceInner(IDXGISwapChain4* a_inner) noexcept
	{
		if (_impl && _impl->proxy) {
			_impl->proxy->ReplaceInner(a_inner);
		}
	}

	bool ReShadeRuntime::EffectsEnabled() const noexcept
	{
		return _impl && _impl->runtime && _impl->runtime->get_effects_state();
	}

	template <class F>
	void ReShadeRuntime::WithColorCopy(ID3D11DeviceContext& a_context, ID3D11Resource& a_color,
		const char* a_event, F&& a_body) noexcept
	{
		if (!_impl || !_impl->runtime) {
			return;
		}
		auto& impl = *_impl;
		cs::render::annotation::ScopedEvent annotationScope(a_event);
		cs::engine::CopyResourcePreservingOM(&a_context, impl.color.get(), &a_color);
		a_body(impl);
		cs::engine::CopyResourcePreservingOM(&a_context, &a_color, impl.color.get());
	}

	void ReShadeRuntime::RenderEffects(ID3D11DeviceContext& a_context, ID3D11Resource& a_color,
		ID3D11ShaderResourceView* a_depth) noexcept
	{
		WithColorCopy(a_context, a_color, "Upscaling::ReShade/Effects", [&](Impl& a_impl) {
			cs::render::ScopedContextState state(&a_context, a_impl.contextState.get());
			if (!state.IsActive()) {
				return;
			}
			a_impl.BindDepth(a_depth);
			a_impl.runtime->render_effects(
				a_impl.runtime->get_command_queue()->get_immediate_command_list(),
				ViewHandle(a_impl.rtv.get()), ViewHandle(a_impl.rtvSrgb.get()));
		});
	}

	void ReShadeRuntime::Present(ID3D11DeviceContext& a_context, ID3D11Resource& a_color) noexcept
	{
		WithColorCopy(a_context, a_color, "Upscaling::ReShade/Present", [](Impl& a_impl) {
			::reshade::update_and_present_effect_runtime(a_impl.runtime);
		});
	}
}
