#include "ReShadeRuntime.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <vector>

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/FullscreenPass.h"
#include "Render/RendererContext.h"
#include "Render/ScopedContextState.h"
#include "Utils/CSUtil.h"
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

		// Every runtime ReShade has, by window: a chain it hooked gets one of its own.
		std::mutex g_registryMutex;
		std::vector<effect_runtime*> g_runtimes;
		std::atomic<HWND> g_ownedWindow{};

		effect_runtime* FindRuntime(HWND a_window, const effect_runtime* a_except = nullptr)
		{
			std::scoped_lock lock(g_registryMutex);
			for (auto* runtime : g_runtimes) {
				if (runtime != a_except && runtime->get_hwnd() == a_window) {
					return runtime;
				}
			}
			return nullptr;
		}

		void CheckForDuplicateRuntimes()
		{
			const auto window = g_ownedWindow.load(std::memory_order_acquire);
			if (!window) {
				return;
			}
			std::scoped_lock lock(g_registryMutex);
			const auto count = std::ranges::count_if(g_runtimes, [&](auto* a_runtime) { return a_runtime->get_hwnd() == window; });
			if (count > 1) {
				L->error("{} ReShade effect runtimes are active for the Community Shaders window; effects will run twice", count);
			}
		}

		void OnInitEffectRuntime(effect_runtime* a_runtime)
		{
			{
				std::scoped_lock lock(g_registryMutex);
				g_runtimes.push_back(a_runtime);
			}
			CheckForDuplicateRuntimes();
		}

		void OnDestroyEffectRuntime(effect_runtime* a_runtime)
		{
			std::scoped_lock lock(g_registryMutex);
			std::erase(g_runtimes, a_runtime);
		}

		// ReShade stays registered for the process: runtimes can exist before any of ours.
		bool EnsureAddon() noexcept
		{
			static const bool registered = [] {
				const auto module = cs::util::ModuleFromAddress(reinterpret_cast<const void*>(&OnInitEffectRuntime));
				if (!::reshade::register_addon(module)) {
					return false;
				}
				::reshade::register_event<::reshade::addon_event::init_effect_runtime>(&OnInitEffectRuntime);
				::reshade::register_event<::reshade::addon_event::destroy_effect_runtime>(&OnDestroyEffectRuntime);
				return true;
			}();
			return registered;
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
		class BackBufferOwner final : public render::IDXGISwapChainProxyOwner
		{
		public:
			BackBufferOwner(render::IDXGISwapChainProxyOwner& a_host, ID3D11Texture2D& a_buffer, bool a_hdr) noexcept :
				_host(a_host), _hdr(a_hdr)
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
				const HRESULT result = _host.GetDesc(a_desc);
				if (SUCCEEDED(result) && _hdr)
					a_desc->BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				return result;
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
				const HRESULT result = _host.GetDesc1(a_desc);
				if (SUCCEEDED(result) && _hdr)
					a_desc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				return result;
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
			render::IDXGISwapChainProxyOwner& _host;
			winrt::com_ptr<ID3D11Texture2D> _buffer;
			bool _hdr;
		};

		// How the effect runtime comes to exist, and who presents it.
		class Attachment
		{
		public:
			virtual ~Attachment() = default;
			[[nodiscard]] virtual effect_runtime* Runtime() noexcept = 0;
			// Draws ReShade's overlay on the copy; the adopted runtime does that at the real Present.
			virtual void Present(effect_runtime& a_runtime) noexcept = 0;
		};

		// A chain ReShade did not hook: CS creates the runtime on a proxy of it and presents it.
		class HostedAttachment final : public Attachment
		{
		public:
			HostedAttachment(render::IDXGISwapChainProxyOwner& a_host, IDXGISwapChain4& a_inner,
				ID3D11Device& a_device, ID3D11Texture2D& a_color, bool a_hdr)
			{
				_owner = std::make_unique<BackBufferOwner>(a_host, a_color, a_hdr);
				_proxy.attach(new render::DXGISwapChainProxy(*_owner, a_inner));
				// A relative path resolves against ReShade's base path, like its own runtime.
				if (!::reshade::create_effect_runtime(::reshade::api::device_api::d3d11, &a_device,
						nullptr, _proxy.get(), "ReShade.ini", &_runtime)) {
					_runtime = nullptr;
					DX::ThrowIfFailed(E_FAIL);
				}
			}
			~HostedAttachment() override
			{
				if (_runtime) {
					::reshade::destroy_effect_runtime(_runtime);
				}
				if (_proxy) {
					_proxy->DetachOwner();
				}
			}
			effect_runtime* Runtime() noexcept override { return _runtime; }
			void Present(effect_runtime& a_runtime) noexcept override { ::reshade::update_and_present_effect_runtime(&a_runtime); }

		private:
			std::unique_ptr<BackBufferOwner> _owner;
			winrt::com_ptr<render::DXGISwapChainProxy> _proxy;
			effect_runtime* _runtime = nullptr;
		};

		// ReShade hooked the chain and made its own runtime; CS only renders effects through it.
		class AdoptedAttachment final : public Attachment
		{
		public:
			explicit AdoptedAttachment(IDXGISwapChain4& a_chain)
			{
				DX::ThrowIfFailed(a_chain.GetHwnd(&_window));
				if (!FindRuntime(_window)) {
					DX::ThrowIfFailed(E_FAIL);
				}
			}
			effect_runtime* Runtime() noexcept override { return FindRuntime(_window); }
			void Present(effect_runtime&) noexcept override {}

		private:
			HWND _window = nullptr;
		};
	}

	struct ReShadeRuntime::Impl
	{
		std::unique_ptr<Attachment> attachment;
		winrt::com_ptr<ID3D11Texture2D> color;
		winrt::com_ptr<ID3D11RenderTargetView> rtv;
		winrt::com_ptr<ID3D11RenderTargetView> rtvSrgb;
		winrt::com_ptr<ID3DDeviceContextState> contextState;
		[[nodiscard]] effect_runtime* Runtime() const noexcept { return attachment ? attachment->Runtime() : nullptr; }
		ID3D11ShaderResourceView* boundDepth = nullptr;
		bool addonRegistered = false;

		// HDR container only: the copy is fp16 and converts to and from scRGB.
		bool hdrContainer = false;
		bool scRgb = false;
		winrt::com_ptr<ID3D11ShaderResourceView> colorSrv;
		render::FullscreenPass convertPass;
		winrt::com_ptr<ID3D11PixelShader> toScRgb;
		winrt::com_ptr<ID3D11PixelShader> fromScRgb;
		winrt::com_ptr<ID3D11Buffer> convertConstants;

		~Impl()
		{
			g_runtime.store(nullptr, std::memory_order_release);
			attachment.reset();
			if (addonRegistered) {
				::reshade::unregister_event<::reshade::addon_event::reshade_reloaded_effects>(&OnReloadedEffects);
			}
		}

		void Create(render::IDXGISwapChainProxyOwner& a_host, IDXGISwapChain4& a_inner,
			ID3D11Device& a_device, UINT a_width, UINT a_height, bool a_hdrContainer, Attach a_attach)
		{
			hdrContainer = a_hdrContainer;
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
			desc.Format = a_hdrContainer ? DXGI_FORMAT_R16G16B16A16_TYPELESS : DXGI_FORMAT_R8G8B8A8_TYPELESS;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, color.put()));
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Format = a_hdrContainer ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
			DX::ThrowIfFailed(device->CreateRenderTargetView(color.get(), &rtvDesc, rtv.put()));
			// fp16 has no sRGB variant; both views write the same values.
			rtvDesc.Format = a_hdrContainer ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			DX::ThrowIfFailed(device->CreateRenderTargetView(color.get(), &rtvDesc, rtvSrgb.put()));
			if (a_hdrContainer) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
				srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(color.get(), &srvDesc, colorSrv.put()));
				convertPass.Create(device.get());
				toScRgb = cs::util::RequireShaderAs<ID3D11PixelShader>("Upscaling/ReShade.ToScRGB", L"Data\\Shaders\\FO4\\Upscaling\\ScRGBConvertPS.hlsl", { { "TO_SCRGB", "1" } });
				fromScRgb = cs::util::RequireShaderAs<ID3D11PixelShader>("Upscaling/ReShade.FromScRGB", L"Data\\Shaders\\FO4\\Upscaling\\ScRGBConvertPS.hlsl");
				D3D11_BUFFER_DESC constants{};
				constants.ByteWidth = 16;
				constants.Usage = D3D11_USAGE_DYNAMIC;
				constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				DX::ThrowIfFailed(device->CreateBuffer(&constants, nullptr, convertConstants.put()));
				cs::render::annotation::SetName(convertConstants.get(), "Upscaling/ReShade.ScRGB.Constants");
			}
			cs::render::annotation::SetName(color.get(), "Upscaling/ReShade.Color");
			cs::render::annotation::SetName(rtv.get(), "Upscaling/ReShade.Color.RTV");
			cs::render::annotation::SetName(rtvSrgb.get(), "Upscaling/ReShade.Color.RTV_sRGB");

			// ReShade rebinds state freely; a private state keeps the engine's intact.
			const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
			D3D_FEATURE_LEVEL selected{};
			DX::ThrowIfFailed(device1->CreateDeviceContextState(
				0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &selected,
				contextState.put()));

			// Events need an add-on identity; without it, rebind every frame.
			addonRegistered = EnsureAddon();
			if (addonRegistered) {
				::reshade::register_event<::reshade::addon_event::reshade_reloaded_effects>(&OnReloadedEffects);
			}
			g_effectsReloaded.store(true, std::memory_order_release);

			if (a_attach == Attach::kAdopt) {
				attachment = std::make_unique<AdoptedAttachment>(a_inner);
			} else {
				attachment = std::make_unique<HostedAttachment>(a_host, a_inner, *device, *color, a_hdrContainer);
			}
			g_runtime.store(Runtime(), std::memory_order_release);
		}

		// Draws a converted copy of a_source into a_target.
		void Convert(ID3D11DeviceContext& a_context, ID3D11ShaderResourceView* a_source, ID3D11RenderTargetView* a_target,
			ID3D11PixelShader* a_shader, float a_whiteNits, UINT a_width, UINT a_height)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(a_context.Map(convertConstants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
				return;
			*static_cast<float*>(mapped.pData) = a_whiteNits / 80.0f;
			a_context.Unmap(convertConstants.get(), 0);
			const render::FullscreenPass::Scope pass(convertPass, &a_context, a_target, a_width, a_height);
			if (!pass.IsActive())
				return;
			ID3D11Buffer* constants = convertConstants.get();
			a_context.PSSetConstantBuffers(0, 1, &constants);
			a_context.PSSetShaderResources(0, 1, &a_source);
			pass.Draw(a_shader);
		}

		// ReShade picks its effect color space from this override.
		void SetScRgb(bool a_scRgb)
		{
			if (scRgb == a_scRgb)
				return;
			scRgb = a_scRgb;
			Runtime()->set_color_space(a_scRgb ? ::reshade::api::color_space::extended_srgb_linear : ::reshade::api::color_space::srgb_nonlinear);
			L->info("ReShade color space set to {}", a_scRgb ? "scRGB" : "sRGB");
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
			Runtime()->update_texture_bindings("DEPTH", handle, handle);
			const bool ready = a_depth != nullptr;
			Runtime()->enumerate_uniform_variables(nullptr, [ready](effect_runtime* a_runtime, ::reshade::api::effect_uniform_variable a_variable) {
				char source[32];
				if (a_runtime->get_annotation_string_from_uniform_variable(a_variable, "source", source) &&
					std::strcmp(source, "bufready_depth") == 0) {
					a_runtime->set_uniform_value_bool(a_variable, ready);
				}
			});
		}
	};

	winrt::com_ptr<ID3D11Device> ReShadeRuntime::NativeDevice(ID3D11Device& a_device) noexcept
	{
		winrt::com_ptr<ID3D11Device> device;
		device.copy_from(&a_device);
		if (!IsInstalled()) {
			return device;
		}
		// ReShade proxies the device but not resources, so a resource reports the device beneath.
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = 1;
		desc.Height = 1;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		winrt::com_ptr<ID3D11Texture2D> probe;
		if (SUCCEEDED(a_device.CreateTexture2D(&desc, nullptr, probe.put()))) {
			winrt::com_ptr<ID3D11Device> native;
			probe->GetDevice(native.put());
			if (native) {
				return native;
			}
		}
		return device;
	}

	void ReShadeRuntime::Arm() noexcept
	{
		if (IsInstalled()) {
			(void)EnsureAddon();
		}
	}

	void ReShadeRuntime::GuardWindow(HWND a_window) noexcept
	{
		if (IsInstalled() && EnsureAddon()) {
			g_ownedWindow.store(a_window, std::memory_order_release);
			CheckForDuplicateRuntimes();
		}
	}

	ReShadeRuntime::ReShadeRuntime() noexcept = default;

	ReShadeRuntime::~ReShadeRuntime() = default;

	bool ReShadeRuntime::IsInstalled() noexcept
	{
		static const bool installed = FindReShade();
		return installed;
	}

	bool ReShadeRuntime::Ensure(render::IDXGISwapChainProxyOwner& a_host, IDXGISwapChain4& a_inner,
		ID3D11Device& a_device, UINT a_width, UINT a_height, bool a_hdrContainer, Attach a_attach) noexcept
	{
		if (_attempted) {
			return _impl != nullptr;
		}
		_attempted = true;
		try {
			_impl = std::make_unique<Impl>();
			_impl->Create(a_host, a_inner, a_device, a_width, a_height, a_hdrContainer, a_attach);
			L->info("ReShade effect runtime created at {}x{}{}", a_width, a_height, a_hdrContainer ? " (fp16 HDR container)" : "");
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

	bool ReShadeRuntime::EffectsEnabled() const noexcept
	{
		const auto* runtime = _impl ? _impl->Runtime() : nullptr;
		return runtime && runtime->get_effects_state();
	}

	template <class F>
	void ReShadeRuntime::WithColorCopy(ID3D11DeviceContext& a_context, ID3D11Resource& a_color,
		const ScRgbProxy* a_scRgb, const char* a_event, F&& a_body) noexcept
	{
		if (!_impl || !_impl->Runtime()) {
			return;
		}
		auto& impl = *_impl;
		cs::render::annotation::ScopedEvent annotationScope(a_event);
		const bool convert = a_scRgb && impl.hdrContainer && a_scRgb->srv && a_scRgb->rtv;
		if (impl.hdrContainer)
			impl.SetScRgb(convert);
		D3D11_TEXTURE2D_DESC desc{};
		static_cast<ID3D11Texture2D&>(a_color).GetDesc(&desc);
		if (convert) {
			impl.Convert(a_context, a_scRgb->srv, impl.rtv.get(), impl.toScRgb.get(), a_scRgb->whiteNits, desc.Width, desc.Height);
		} else {
			cs::engine::CopyResourcePreservingOM(&a_context, impl.color.get(), &a_color);
		}
		a_body(impl);
		if (convert) {
			impl.Convert(a_context, impl.colorSrv.get(), a_scRgb->rtv, impl.fromScRgb.get(), a_scRgb->whiteNits, desc.Width, desc.Height);
		} else {
			cs::engine::CopyResourcePreservingOM(&a_context, &a_color, impl.color.get());
		}
	}

	void ReShadeRuntime::RenderEffects(ID3D11DeviceContext& a_context, ID3D11Resource& a_color,
		ID3D11ShaderResourceView* a_depth, const ScRgbProxy* a_scRgb) noexcept
	{
		static std::atomic_uint traced{ 0 };
		const bool trace = traced.fetch_add(1) < 3;
		if (trace)
			L->info("ReShade render_effects begin");
		WithColorCopy(a_context, a_color, a_scRgb, "Upscaling::ReShade/Effects", [&](Impl& a_impl) {
			cs::render::ScopedContextState state(&a_context, a_impl.contextState.get());
			if (!state.IsActive()) {
				return;
			}
			a_impl.BindDepth(a_depth);
			auto* runtime = a_impl.Runtime();
			runtime->render_effects(
				runtime->get_command_queue()->get_immediate_command_list(),
				ViewHandle(a_impl.rtv.get()), ViewHandle(a_impl.rtvSrgb.get()));
		});
		if (trace)
			L->info("ReShade render_effects end");
	}

	void ReShadeRuntime::Present(ID3D11DeviceContext& a_context, ID3D11Resource& a_color, const ScRgbProxy* a_scRgb) noexcept
	{
		static std::atomic_uint traced{ 0 };
		const bool trace = traced.fetch_add(1) < 3;
		if (trace)
			L->info("ReShade update_and_present begin");
		WithColorCopy(a_context, a_color, a_scRgb, "Upscaling::ReShade/Present", [](Impl& a_impl) {
			a_impl.attachment->Present(*a_impl.Runtime());
		});
		if (trace)
			L->info("ReShade update_and_present end");
	}
}
