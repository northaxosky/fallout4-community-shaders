#pragma once

#include <DirectXMath.h>
#include <d3d11.h>

#include <winrt/base.h>

#include "CSBuffer.h"

namespace cs::features::imagespace
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

		void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
		{
			auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
		}

		void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
		{
			auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
		}

		D3D11_TEXTURE2D_DESC desc;
		winrt::com_ptr<ID3D11Texture2D> resource;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	};

}
