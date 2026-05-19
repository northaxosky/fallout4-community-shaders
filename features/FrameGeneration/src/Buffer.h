#pragma once

#include <DirectXMath.h>
#include <d3d11.h>

#include <Windows.Foundation.h>
#include <stdio.h>
#include <winrt/base.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include "CSBuffer.h"

namespace cs::features::framegeneration
{

using cs::buffer::ConstantBuffer;
using cs::buffer::ConstantBufferDesc;
using cs::buffer::GetCBufferSize;

class Texture2D
{
public:
	explicit Texture2D(D3D11_TEXTURE2D_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, resource.put()));
	}

	explicit Texture2D(ID3D11Texture2D* a_resource)
	{
		a_resource->GetDesc(&desc);
		resource.attach(a_resource);
	}

	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, rtv.put()));
	}

	D3D11_TEXTURE2D_DESC desc;
	winrt::com_ptr<ID3D11Texture2D> resource;
	winrt::com_ptr<ID3D11ShaderResourceView> srv;
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
};

}
