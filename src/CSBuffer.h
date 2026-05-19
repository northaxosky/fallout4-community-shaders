#pragma once

#include <DirectXMath.h>
#include <d3d11.h>

#include <winrt/base.h>

namespace cs::buffer
{
	[[nodiscard]] static constexpr std::uint32_t GetCBufferSize(std::uint32_t a_size)
	{
		return (a_size + (64 - 1)) & ~(64 - 1);
	}

	[[nodiscard]] inline D3D11_BUFFER_DESC ConstantBufferDesc(uint32_t a_size, bool a_dynamic = true)
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = a_dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = a_dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
		desc.ByteWidth = GetCBufferSize(a_size);
		return desc;
	}

	template <typename T>
	[[nodiscard]] D3D11_BUFFER_DESC ConstantBufferDesc(bool a_dynamic = true)
	{
		return ConstantBufferDesc(sizeof(T), a_dynamic);
	}

	class ConstantBuffer
	{
	public:
		explicit ConstantBuffer(D3D11_BUFFER_DESC const& a_desc) :
			desc(a_desc)
		{
			auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, resource.put()));
		}

		ID3D11Buffer* CB() const { return resource.get(); }

		void Update(void const* a_srcData, size_t a_dataSize)
		{
			auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(RE::BSGraphics::GetRendererData()->context);
			if (desc.Usage & D3D11_USAGE_DYNAMIC) {
				D3D11_MAPPED_SUBRESOURCE mapped{};
				DX::ThrowIfFailed(ctx->Map(resource.get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped));
				memcpy(mapped.pData, a_srcData, a_dataSize);
				ctx->Unmap(resource.get(), 0);
			} else {
				ctx->UpdateSubresource(resource.get(), 0, nullptr, a_srcData, 0, 0);
			}
		}

		template <typename T>
		void Update(T const& a_srcData) { Update(&a_srcData, sizeof(T)); }

	private:
		winrt::com_ptr<ID3D11Buffer> resource;
		D3D11_BUFFER_DESC desc;
	};
}
