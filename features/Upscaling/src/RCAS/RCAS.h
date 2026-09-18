#pragma once

#include "Utils/CSBuffer.h"

#include <d3d11_4.h>
#include <d3d12.h>
#include <winrt/base.h>

namespace cs::features
{
	class RCAS
	{
	public:
		RCAS() = default;
		~RCAS();

		void Initialize();
		bool ApplySharpen(ID3D11ShaderResourceView* inputTexture,
			ID3D11UnorderedAccessView* outputUAV, float sharpness);
		bool InitializeD3D12(ID3D12Device* device);
		void ResetD3D12() noexcept;
		bool RecordSharpen(ID3D12GraphicsCommandList* commandList,
			winrt::com_ptr<ID3D12DescriptorHeap>& descriptors,
			ID3D12Resource* inputTexture, D3D12_RESOURCE_STATES inputState,
			ID3D12Resource* outputTexture, D3D12_RESOURCE_STATES outputState,
			std::uint32_t width, std::uint32_t height, float sharpness);

	private:
		[[nodiscard]] static float ResolveSharpness(float sharpness) noexcept;

		winrt::com_ptr<ID3D11ComputeShader> rcasComputeShader;
		cs::buffer::ConstantBuffer* rcasConfigCB = nullptr;
		winrt::com_ptr<ID3D12Device> rcasDevice12;
		winrt::com_ptr<ID3D12RootSignature> rcasRootSignature12;
		winrt::com_ptr<ID3D12PipelineState> rcasPipeline12;
		UINT rcasDescriptorSize12 = 0;
	};
}
