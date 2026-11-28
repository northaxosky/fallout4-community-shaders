#include "Render/ComputePasses.h"

#include "Render/Annotation.h"

#include <format>

namespace cs::render
{
	std::unique_ptr<buffer::Texture2D> CreateComputeTexture(
		UINT a_width, UINT a_height, DXGI_FORMAT a_format,
		std::string_view a_name, UINT a_mips)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = a_mips;
		desc.ArraySize = 1;
		desc.Format = a_format;
		desc.SampleDesc.Count = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		auto result = std::make_unique<buffer::Texture2D>(desc);
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = a_format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = a_mips;
		result->CreateSRV(srv);
		if (a_mips == 1) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = a_format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			result->CreateUAV(uav);
		}
		result->SetName(a_name, std::format("{}.SRV", a_name), std::format("{}.UAV", a_name));
		return result;
	}

	ComputePasses::ComputePasses(ID3D11DeviceContext* a_context, UINT a_preservedSlot, ID3D11Buffer* a_buffer) :
		ComputePasses(a_context)
	{
		_bufferSlot = a_preservedSlot;
		_context->CSGetConstantBuffers(_bufferSlot, 1, _engineBuffer.put());
		_context->CSSetConstantBuffers(_bufferSlot, 1, &a_buffer);
	}

	ComputePasses::ComputePasses(ID3D11DeviceContext* a_context) :
		_context(a_context)
	{
		ID3D11ShaderResourceView* engine[kPreservedSrvs]{};
		_context->CSGetShaderResources(kScopeSrvs, kPreservedSrvs, engine);
		for (UINT i = 0; i < kPreservedSrvs; ++i)
			_engineSRVs[i].attach(engine[i]);
		ID3D11ShaderResourceView* nullSRVs[kScopeSrvs]{};
		ID3D11UnorderedAccessView* nullUAVs[6]{};
		_context->CSSetShaderResources(0, kScopeSrvs, nullSRVs);
		_context->CSSetUnorderedAccessViews(0, 6, nullUAVs, nullptr);
	}

	ComputePasses::~ComputePasses()
	{
		ID3D11ShaderResourceView* engine[kPreservedSrvs]{};
		for (UINT i = 0; i < kPreservedSrvs; ++i)
			engine[i] = _engineSRVs[i].get();
		_context->CSSetShaderResources(kScopeSrvs, kPreservedSrvs, engine);
		if (_bufferSlot == kNoSlot)
			return;
		auto* engineBuffer = _engineBuffer.get();
		_context->CSSetConstantBuffers(_bufferSlot, 1, &engineBuffer);
	}

	void ComputePasses::Dispatch(ID3D11ComputeShader* a_shader,
		std::span<ID3D11ShaderResourceView* const> a_srvs,
		std::span<ID3D11UnorderedAccessView* const> a_uavs,
		UINT a_width, UINT a_height, UINT a_group, std::string_view a_name)
	{
		annotation::ScopedEvent event(a_name);
		_context->CSSetShaderResources(0, static_cast<UINT>(a_srvs.size()), a_srvs.data());
		_context->CSSetUnorderedAccessViews(0, static_cast<UINT>(a_uavs.size()), a_uavs.data(), nullptr);
		_context->CSSetShader(a_shader, nullptr, 0);
		_context->Dispatch((a_width + a_group - 1) / a_group, (a_height + a_group - 1) / a_group, 1);
		static constexpr ID3D11ShaderResourceView* nullSRVs[kSrvSlots]{};
		static constexpr ID3D11UnorderedAccessView* nullUAVs[kUavSlots]{};
		_context->CSSetShaderResources(0, static_cast<UINT>(a_srvs.size()), nullSRVs);
		_context->CSSetUnorderedAccessViews(0, static_cast<UINT>(a_uavs.size()), nullUAVs, nullptr);
	}
}
