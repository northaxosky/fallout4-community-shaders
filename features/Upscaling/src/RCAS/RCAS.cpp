#include "RCAS.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Utils/ShaderCompile.h"

#include <array>
#include <cmath>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.rcas");

		struct RCASConfig
		{
			float sharpness;
			float3 pad;
		};

		D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* a_resource,
			D3D12_RESOURCE_STATES a_before,
			D3D12_RESOURCE_STATES a_after) noexcept
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = a_resource;
			barrier.Transition.StateBefore = a_before;
			barrier.Transition.StateAfter = a_after;
			barrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			return barrier;
		}
	}

	float RCAS::ResolveSharpness(float a_sharpness) noexcept
	{
		return std::exp2(-((-2.0f * a_sharpness) + 2.0f));
	}

	bool RCAS::InitializeD3D12(ID3D12Device* a_device)
	{
		if (!a_device) {
			return false;
		}
		if (rcasDevice12.get() == a_device && rcasRootSignature12 &&
			rcasPipeline12) {
			return true;
		}
		ResetD3D12();

		std::string compileError;
		auto shader = cs::util::CompileShaderToBlob(
			L"Data\\Shaders\\Upscaling\\RCAS\\RCAS.hlsl", {}, "cs_5_1",
			"main", &compileError);
		if (!shader) {
			L->error("Could not compile D3D12 RCAS: {}", compileError);
			return false;
		}

		D3D12_DESCRIPTOR_RANGE ranges[2]{};
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].NumDescriptors = 1;
		ranges[0].BaseShaderRegister = 0;
		ranges[0].RegisterSpace = 0;
		ranges[0].OffsetInDescriptorsFromTableStart = 0;
		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		ranges[1].NumDescriptors = 1;
		ranges[1].BaseShaderRegister = 0;
		ranges[1].RegisterSpace = 0;
		ranges[1].OffsetInDescriptorsFromTableStart = 0;

		D3D12_ROOT_PARAMETER parameters[3]{};
		parameters[0].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[0].Constants.ShaderRegister = 0;
		parameters[0].Constants.RegisterSpace = 0;
		parameters[0].Constants.Num32BitValues = 4;
		parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		parameters[1].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[1].DescriptorTable.NumDescriptorRanges = 1;
		parameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
		parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		parameters[2].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[2].DescriptorTable.NumDescriptorRanges = 1;
		parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
		parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC rootDesc{};
		rootDesc.NumParameters = static_cast<UINT>(std::size(parameters));
		rootDesc.pParameters = parameters;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		Microsoft::WRL::ComPtr<ID3DBlob> serialized;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		const HRESULT serializeResult = D3D12SerializeRootSignature(
			&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serialized.GetAddressOf(), errors.GetAddressOf());
		if (FAILED(serializeResult)) {
			const std::string detail = errors
				? std::string(
					  static_cast<const char*>(errors->GetBufferPointer()),
					  errors->GetBufferSize())
				: std::string{};
			L->error("Could not serialize D3D12 RCAS root signature: {}",
				detail);
			return false;
		}
		if (FAILED(a_device->CreateRootSignature(
				0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
				IID_PPV_ARGS(rcasRootSignature12.put())))) {
			L->error("Could not create D3D12 RCAS root signature");
			ResetD3D12();
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = rcasRootSignature12.get();
		pipelineDesc.CS = {
			.pShaderBytecode = shader->GetBufferPointer(),
			.BytecodeLength = shader->GetBufferSize()
		};
		if (FAILED(a_device->CreateComputePipelineState(
				&pipelineDesc, IID_PPV_ARGS(rcasPipeline12.put())))) {
			L->error("Could not create D3D12 RCAS pipeline state");
			ResetD3D12();
			return false;
		}

		rcasDevice12.copy_from(a_device);
		rcasDescriptorSize12 = a_device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		if (!rcasDescriptorSize12) {
			ResetD3D12();
			return false;
		}
		cs::render::annotation::SetName(
			rcasRootSignature12.get(), "Upscaling/RCAS.RootSignature12");
		cs::render::annotation::SetName(
			rcasPipeline12.get(), "Upscaling/RCAS.Pipeline12");
		return true;
	}

	void RCAS::ResetD3D12() noexcept
	{
		rcasPipeline12 = nullptr;
		rcasRootSignature12 = nullptr;
		rcasDevice12 = nullptr;
		rcasDescriptorSize12 = 0;
	}

	bool RCAS::RecordSharpen(ID3D12GraphicsCommandList* a_commandList,
		winrt::com_ptr<ID3D12DescriptorHeap>& a_descriptors,
		ID3D12Resource* a_inputTexture, D3D12_RESOURCE_STATES a_inputState,
		ID3D12Resource* a_outputTexture, D3D12_RESOURCE_STATES a_outputState,
		std::uint32_t a_width, std::uint32_t a_height, float a_sharpness)
	{
		if (!a_commandList || !rcasDevice12 || !rcasRootSignature12 ||
			!rcasPipeline12 || !rcasDescriptorSize12 || !a_inputTexture ||
			!a_outputTexture || a_inputTexture == a_outputTexture || !a_width ||
			!a_height) {
			return false;
		}

		const auto inputDesc = a_inputTexture->GetDesc();
		const auto outputDesc = a_outputTexture->GetDesc();
		if (inputDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			outputDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			inputDesc.Format != outputDesc.Format ||
			inputDesc.DepthOrArraySize != 1 ||
			outputDesc.DepthOrArraySize != 1 ||
			inputDesc.SampleDesc.Count != 1 ||
			outputDesc.SampleDesc.Count != 1 ||
			a_width > inputDesc.Width || a_height > inputDesc.Height ||
			a_width > outputDesc.Width || a_height > outputDesc.Height ||
			(outputDesc.Flags &
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
			return false;
		}

		if (!a_descriptors) {
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.NumDescriptors = 2;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			if (FAILED(rcasDevice12->CreateDescriptorHeap(
					&heapDesc, IID_PPV_ARGS(a_descriptors.put())))) {
				L->error("Could not create D3D12 RCAS descriptors");
				return false;
			}
		}

		auto inputCpu =
			a_descriptors->GetCPUDescriptorHandleForHeapStart();
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = inputDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping =
			D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		rcasDevice12->CreateShaderResourceView(
			a_inputTexture, &srvDesc, inputCpu);

		auto outputCpu = inputCpu;
		outputCpu.ptr += rcasDescriptorSize12;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = outputDesc.Format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		rcasDevice12->CreateUnorderedAccessView(
			a_outputTexture, nullptr, &uavDesc, outputCpu);

		std::array<D3D12_RESOURCE_BARRIER, 2> before{};
		UINT beforeCount = 0;
		if (a_inputState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
			before[beforeCount++] = Transition(a_inputTexture, a_inputState,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
		if (a_outputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
			before[beforeCount++] = Transition(a_outputTexture, a_outputState,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		if (beforeCount) {
			a_commandList->ResourceBarrier(beforeCount, before.data());
		}

		ID3D12DescriptorHeap* descriptorHeaps[]{ a_descriptors.get() };
		a_commandList->SetDescriptorHeaps(
			static_cast<UINT>(std::size(descriptorHeaps)), descriptorHeaps);
		a_commandList->SetComputeRootSignature(rcasRootSignature12.get());
		a_commandList->SetPipelineState(rcasPipeline12.get());
		const RCASConfig config{ .sharpness = ResolveSharpness(a_sharpness) };
		a_commandList->SetComputeRoot32BitConstants(
			0, 4, &config, 0);
		auto inputGpu =
			a_descriptors->GetGPUDescriptorHandleForHeapStart();
		auto outputGpu = inputGpu;
		outputGpu.ptr += rcasDescriptorSize12;
		a_commandList->SetComputeRootDescriptorTable(1, inputGpu);
		a_commandList->SetComputeRootDescriptorTable(2, outputGpu);
		{
			cs::render::annotation::ScopedEvent annotationScope(
				a_commandList, "Upscaling/RCAS");
			a_commandList->Dispatch(
				(a_width + 7) / 8, (a_height + 7) / 8, 1);
		}

		D3D12_RESOURCE_BARRIER outputBarrier{};
		outputBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		outputBarrier.UAV.pResource = a_outputTexture;
		a_commandList->ResourceBarrier(1, &outputBarrier);

		std::array<D3D12_RESOURCE_BARRIER, 2> after{};
		UINT afterCount = 0;
		if (a_inputState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
			after[afterCount++] = Transition(a_inputTexture,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, a_inputState);
		}
		if (a_outputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
			after[afterCount++] = Transition(a_outputTexture,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS, a_outputState);
		}
		if (afterCount) {
			a_commandList->ResourceBarrier(afterCount, after.data());
		}
		return true;
	}
}
