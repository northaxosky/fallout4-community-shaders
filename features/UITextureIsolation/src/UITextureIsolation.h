#pragma once

#include "Feature.h"

#include <d3d11.h>

namespace cs::features
{
	class UITextureIsolation : public Feature
	{
	public:
		static UITextureIsolation* Get();

		std::string_view GetName() const override { return "UITextureIsolation"; }

		void Load() override;
		void OnD3D11Ready(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
		void OnPresent();

		// Reads engine RTV pointers fresh each call to survive resource recreation.
		bool MatchesEngineUI(ID3D11RenderTargetView* a_rtv) const noexcept;

		ID3D11Texture2D*          GetD3D11Texture() const noexcept { return _texture; }
		ID3D11ShaderResourceView* GetD3D11SRV() const noexcept     { return _srv; }
		UINT                      GetWidth() const noexcept        { return _width; }
		UINT                      GetHeight() const noexcept       { return _height; }
		DXGI_FORMAT               GetFormat() const noexcept       { return _format; }

	private:
		void Reallocate(const D3D11_TEXTURE2D_DESC& a_engineDesc);
		void Release();

		ID3D11Device*             _device   = nullptr;
		ID3D11DeviceContext*      _context  = nullptr;
		ID3D11Texture2D*          _texture  = nullptr;
		ID3D11ShaderResourceView* _srv      = nullptr;
		UINT                      _width    = 0;
		UINT                      _height   = 0;
		DXGI_FORMAT               _format   = DXGI_FORMAT_UNKNOWN;
	};

	namespace UITextureIsolationDetail
	{
		void InstallOMSetRenderTargetsObserver(ID3D11DeviceContext* a_context);
		bool ConsumeUIActiveFlag();
	}
}
