#include "Render/FullscreenPass.h"

#include "Utils/CSUtil.h"

namespace cs::render
{
	void FullscreenPass::Create(ID3D11Device* a_device)
	{
		winrt::com_ptr<ID3D11Device1> device;
		DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(device.put())));
		const auto level = a_device->GetFeatureLevel();
		D3D_FEATURE_LEVEL selected{};
		DX::ThrowIfFailed(device->CreateDeviceContextState(
			0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &selected, _state.put()));
		_vertexShader.attach(static_cast<ID3D11VertexShader*>(
			util::CompileShader(L"Data\\Shaders\\FO4\\FullscreenVS.hlsl", {}, "vs_5_0")));
		D3D11_RASTERIZER_DESC rasterizer{};
		rasterizer.FillMode = D3D11_FILL_SOLID;
		rasterizer.CullMode = D3D11_CULL_NONE;
		rasterizer.DepthClipEnable = TRUE;
		DX::ThrowIfFailed(a_device->CreateRasterizerState(&rasterizer, _rasterizer.put()));
	}

	FullscreenPass::Scope::Scope(
		const FullscreenPass& a_pass, ID3D11DeviceContext* a_context, ID3D11RenderTargetView* a_target,
		std::uint32_t a_width, std::uint32_t a_height) :
		_state(a_context, a_pass._state.get()),
		_context(a_context)
	{
		if (!_state.IsActive())
			return;
		_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		_context->VSSetShader(a_pass._vertexShader.get(), nullptr, 0);
		_context->RSSetState(a_pass._rasterizer.get());
		const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(a_width), static_cast<float>(a_height), 0, 1 };
		_context->RSSetViewports(1, &viewport);
		_context->OMSetRenderTargets(1, &a_target, nullptr);
	}

	void FullscreenPass::Scope::Draw(ID3D11PixelShader* a_shader) const
	{
		_context->PSSetShader(a_shader, nullptr, 0);
		_context->Draw(3, 0);
	}
}
