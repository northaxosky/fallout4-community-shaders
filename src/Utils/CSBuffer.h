#pragma once

#include <DirectXMath.h>
#include <d3d11.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <winrt/base.h>

#include "Render/Annotation.h"

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
		void SetName(std::string_view a_name) const
		{
			cs::render::annotation::SetName(resource.get(), a_name);
		}

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
		void Update(T const& a_srcData)
		{
			Update(&a_srcData, sizeof(T));
		}

	private:
		winrt::com_ptr<ID3D11Buffer> resource;
		D3D11_BUFFER_DESC desc;
	};

	// Constant buffer that maps only when bytes differ from the last upload.
	template <typename T>
	class CachedConstantBuffer
	{
	public:
		void Create(std::string_view a_name)
		{
			_buffer = std::make_unique<ConstantBuffer>(ConstantBufferDesc<T>());
			_buffer->SetName(a_name);
			_uploaded.reset();
		}

		ID3D11Buffer* CB() const { return _buffer ? _buffer->CB() : nullptr; }

		ID3D11Buffer* Update(const T& a_next)
		{
			if (!_buffer)
				return nullptr;
			if (!_uploaded || std::memcmp(&*_uploaded, &a_next, sizeof(T)) != 0) {
				_uploaded.reset();  // a throwing Map must not leave a stale cache
				_buffer->Update(a_next);
				_uploaded = a_next;
			}
			return _buffer->CB();
		}

	private:
		std::unique_ptr<ConstantBuffer> _buffer;
		std::optional<T> _uploaded;
	};

	template <typename T>
	[[nodiscard]] D3D11_BUFFER_DESC StructuredBufferDesc(std::uint32_t a_count, bool a_uav = true, bool a_dynamic = false)
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = (a_uav || !a_dynamic) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (a_uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.CPUAccessFlags = a_dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
		desc.StructureByteStride = sizeof(T);
		desc.ByteWidth = sizeof(T) * a_count;
		return desc;
	}

	class StructuredBuffer
	{
	public:
		StructuredBuffer(D3D11_BUFFER_DESC const& a_desc, std::uint32_t a_count) :
			desc(a_desc), count(a_count)
		{
			auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, resource.put()));
		}

		void SetName(std::string_view a_name) const
		{
			const std::string name(a_name);
			cs::render::annotation::SetName(resource.get(), name);
			for (auto& view : srvs)
				cs::render::annotation::SetName(view.get(), name + " SRV");
			for (auto& view : uavs)
				cs::render::annotation::SetName(view.get(), name + " UAV");
		}

		ID3D11Buffer* Resource() const { return resource.get(); }
		ID3D11ShaderResourceView* SRV(std::size_t a_index = 0) const { return srvs[a_index].get(); }
		ID3D11UnorderedAccessView* UAV(std::size_t a_index = 0) const { return uavs[a_index].get(); }

		void CreateSRV()
		{
			auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
			viewDesc.Format = DXGI_FORMAT_UNKNOWN;
			viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			viewDesc.BufferEx.FirstElement = 0;
			viewDesc.BufferEx.NumElements = count;
			winrt::com_ptr<ID3D11ShaderResourceView> view;
			DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &viewDesc, view.put()));
			srvs.push_back(std::move(view));
		}

		void CreateUAV()
		{
			auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
			viewDesc.Format = DXGI_FORMAT_UNKNOWN;
			viewDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			viewDesc.Buffer.FirstElement = 0;
			viewDesc.Buffer.NumElements = count;
			winrt::com_ptr<ID3D11UnorderedAccessView> view;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &viewDesc, view.put()));
			uavs.push_back(std::move(view));
		}

		// Write-discard upload of a_size bytes; buffer must be dynamic.
		void Update(void const* a_srcData, std::size_t a_size)
		{
			auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(RE::BSGraphics::GetRendererData()->context);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			DX::ThrowIfFailed(ctx->Map(resource.get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped));
			memcpy(mapped.pData, a_srcData, a_size);
			ctx->Unmap(resource.get(), 0);
		}

	private:
		winrt::com_ptr<ID3D11Buffer> resource;
		std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> srvs;
		std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> uavs;
		D3D11_BUFFER_DESC desc;
		std::uint32_t count;
	};

	class Texture3D
	{
	public:
		Texture3D(ID3D11Device* a_device, const D3D11_TEXTURE3D_DESC& a_desc, bool a_writable)
		{
			DX::ThrowIfFailed(a_device->CreateTexture3D(&a_desc, nullptr, resource.put()));
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(resource.get(), nullptr, srv.put()));
			if (a_writable)
				DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(resource.get(), nullptr, uav.put()));
		}
		void SetName(std::string_view a_name) const
		{
			cs::render::annotation::SetName(resource.get(), a_name);
		}
		winrt::com_ptr<ID3D11Texture3D> resource;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	};

	class Texture2D
	{
	public:
		explicit Texture2D(D3D11_TEXTURE2D_DESC const& a_desc) :
			desc(a_desc)
		{
			auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, resource.put()));
		}

		explicit Texture2D(ID3D11Texture2D* a_resource)
		{
			a_resource->GetDesc(&desc);
			resource.attach(a_resource);
		}

		void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
		{
			auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
		}

		void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
		{
			auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
		}

		void Reset()
		{
			rtv = nullptr;
			uav = nullptr;
			srv = nullptr;
			resource12 = nullptr;
			resource = nullptr;
		}

		void SetName(
			std::string_view a_texture,
			std::string_view a_srv,
			std::string_view a_uav = {},
			std::string_view a_rtv = {}) const
		{
			cs::render::annotation::SetName(resource.get(), a_texture);
			cs::render::annotation::SetName(srv.get(), a_srv);
			cs::render::annotation::SetName(uav.get(), a_uav);
			cs::render::annotation::SetName(rtv.get(), a_rtv);
		}

		D3D11_TEXTURE2D_DESC desc;
		winrt::com_ptr<ID3D11Texture2D> resource;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		winrt::com_ptr<ID3D11RenderTargetView> rtv;
		winrt::com_ptr<ID3D12Resource> resource12;
	};
}
