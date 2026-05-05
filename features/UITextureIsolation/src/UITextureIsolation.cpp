#include "UITextureIsolation.h"

#include "Log.h"

#include <atomic>

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.uitex"); }

	static constexpr unsigned int kEngineUIIndex     = 17;
	static constexpr unsigned int kEngineUITempIndex = 18;

	// Sampled snapshot logging mirrors the hook's log cadence.
	static constexpr int kSnapLogFirst = 10;
	static constexpr int kSnapLogEvery = 5000;
	static std::atomic<int> s_snapshotCount{ 0 };

	UITextureIsolation* UITextureIsolation::Get()
	{
		static UITextureIsolation instance;
		return &instance;
	}

	void UITextureIsolation::Load()
	{
		L->info("Loaded; observation hook deferred until D3D11 device available");
	}

	void UITextureIsolation::OnD3D11Ready(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		_device  = a_device;
		_context = a_context;
		UITextureIsolationDetail::InstallOMSetRenderTargetsObserver(a_context);
	}

	bool UITextureIsolation::MatchesEngineUI(ID3D11RenderTargetView* a_rtv) const noexcept
	{
		if (!a_rtv)
			return false;
		auto* rd = RE::BSGraphics::GetRendererData();
		if (!rd)
			return false;
		auto* uiRTV     = reinterpret_cast<ID3D11RenderTargetView*>(rd->renderTargets[kEngineUIIndex].rtView);
		auto* uiTempRTV = reinterpret_cast<ID3D11RenderTargetView*>(rd->renderTargets[kEngineUITempIndex].rtView);
		return a_rtv == uiRTV || a_rtv == uiTempRTV;
	}

	void UITextureIsolation::OnPresent()
	{
		if (!UITextureIsolationDetail::ConsumeUIActiveFlag())
			return;
		if (!_device || !_context)
			return;

		auto* rd = RE::BSGraphics::GetRendererData();
		if (!rd)
			return;
		auto* engineUITex = reinterpret_cast<ID3D11Texture2D*>(rd->renderTargets[kEngineUIIndex].texture);
		if (!engineUITex)
			return;

		D3D11_TEXTURE2D_DESC engineDesc{};
		engineUITex->GetDesc(&engineDesc);

		if (!_texture || _width != engineDesc.Width || _height != engineDesc.Height || _format != engineDesc.Format)
			Reallocate(engineDesc);
		if (!_texture)
			return;

		_context->CopyResource(_texture, engineUITex);

		int snap = s_snapshotCount.fetch_add(1, std::memory_order_relaxed);
		if (snap < kSnapLogFirst || (snap % kSnapLogEvery) == 0) {
			L->info("Snapshot copied (count {}, {}x{} format={})",
				snap + 1, _width, _height, static_cast<uint>(_format));
		}
	}

	void UITextureIsolation::Reallocate(const D3D11_TEXTURE2D_DESC& a_engineDesc)
	{
		Release();

		D3D11_TEXTURE2D_DESC desc = a_engineDesc;
		desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		desc.Usage          = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;

		HRESULT hr = _device->CreateTexture2D(&desc, nullptr, &_texture);
		if (FAILED(hr) || !_texture) {
			L->error("CreateTexture2D failed: {:#x}", static_cast<uint32_t>(hr));
			return;
		}

		// SRV creation can fail on TYPELESS source formats; the texture stays valid for CopyResource so we don't tear down.
		hr = _device->CreateShaderResourceView(_texture, nullptr, &_srv);
		if (FAILED(hr)) {
			L->warn("CreateShaderResourceView failed: {:#x} (texture kept; consumers must create their own SRV)", static_cast<uint32_t>(hr));
			_srv = nullptr;
		}

		_width  = desc.Width;
		_height = desc.Height;
		_format = desc.Format;

		L->info("Allocated private UI texture {}x{} format={}", _width, _height, static_cast<uint>(_format));
	}

	void UITextureIsolation::Release()
	{
		if (_srv) { _srv->Release(); _srv = nullptr; }
		if (_texture) { _texture->Release(); _texture = nullptr; }
		_width = 0;
		_height = 0;
		_format = DXGI_FORMAT_UNKNOWN;
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				FeatureManager::Get().Register(UITextureIsolation::Get());
			}
		};
		static AutoRegister _autoRegister;
	}
}
