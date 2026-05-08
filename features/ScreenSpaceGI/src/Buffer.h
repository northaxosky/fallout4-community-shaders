#pragma once

#include <DirectXMath.h>
#include <d3d11.h>

#include <winrt/base.h>

namespace cs::features::ssgi
{

static constexpr std::uint32_t GetCBufferSize(std::uint32_t buffer_size)
{
	return (buffer_size + (64 - 1)) & ~(64 - 1);
}

inline D3D11_BUFFER_DESC ConstantBufferDesc(uint32_t size, bool dynamic = true)
{
	D3D11_BUFFER_DESC desc{};
	desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
	desc.ByteWidth = GetCBufferSize(size);
	return desc;
}

class ConstantBuffer
{
public:
	explicit ConstantBuffer(D3D11_BUFFER_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, resource.put()));
	}

	ID3D11Buffer* CB() const { return resource.get(); }

	void Update(void const* src_data, size_t data_size)
	{
		ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(RE::BSGraphics::GetRendererData()->context);
		if (desc.Usage & D3D11_USAGE_DYNAMIC) {
			D3D11_MAPPED_SUBRESOURCE mapped{};
			DX::ThrowIfFailed(ctx->Map(resource.get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped));
			memcpy(mapped.pData, src_data, data_size);
			ctx->Unmap(resource.get(), 0);
		} else {
			ctx->UpdateSubresource(resource.get(), 0, nullptr, src_data, 0, 0);
		}
	}

	template <typename T>
	void Update(T const& src_data) { Update(&src_data, sizeof(T)); }

private:
	winrt::com_ptr<ID3D11Buffer> resource;
	D3D11_BUFFER_DESC desc;
};

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
