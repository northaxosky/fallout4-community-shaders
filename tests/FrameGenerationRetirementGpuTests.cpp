#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include <d3d11_4.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include "AgilityBootstrap.h"
#include "Render/FrameGenerationOrchestration.h"

namespace
{
	constexpr std::array kRequestedFeatureLevels{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	struct DeviceBundle
	{
		winrt::com_ptr<IDXGIAdapter1> adapter;
		winrt::com_ptr<ID3D12Device> device12;
		winrt::com_ptr<ID3D12CommandQueue> queue;
		winrt::com_ptr<ID3D11Device5> device11;
		winrt::com_ptr<ID3D11DeviceContext4> context11;
		winrt::com_ptr<ID3D12DeviceFactory> deviceFactory;
		cs::features::AgilityBootstrapDiagnostics agility;
		D3D_FEATURE_LEVEL featureLevel11{ D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL featureLevel12{ D3D_FEATURE_LEVEL_11_0 };
	};

	struct SharedTexture
	{
		winrt::com_ptr<ID3D11Texture2D> texture11;
		winrt::com_ptr<ID3D12Resource> resource12;
	};

	struct Readback12
	{
		winrt::com_ptr<ID3D12Resource> resource;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT64 size{};
	};

	struct CommandRecording
	{
		winrt::com_ptr<ID3D12CommandAllocator> allocator;
		winrt::com_ptr<ID3D12GraphicsCommandList> list;
	};

	struct FormatCase
	{
		const char* name;
		DXGI_FORMAT format;
		std::array<float, 4> producerValue;
		std::array<float, 4> consumerValue;
	};

	enum class CompletionStrategy
	{
		kVendorFence,
		kQueueOrdered,
		kWrongFence
	};

	constexpr std::array kFormatCases{
		FormatCase{
			.name = "R8G8B8A8_UNORM",
			.format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.producerValue = { 0.0F, 1.0F, 0.0F, 1.0F },
			.consumerValue = { 1.0F, 0.0F, 1.0F, 1.0F }
		},
		FormatCase{
			.name = "R32_FLOAT",
			.format = DXGI_FORMAT_R32_FLOAT,
			.producerValue = { 0.25F, 0.0F, 0.0F, 0.0F },
			.consumerValue = { 0.75F, 0.0F, 0.0F, 0.0F }
		},
		FormatCase{
			.name = "R16G16_FLOAT",
			.format = DXGI_FORMAT_R16G16_FLOAT,
			.producerValue = { 0.25F, 0.5F, 0.0F, 0.0F },
			.consumerValue = { 0.75F, 0.125F, 0.0F, 0.0F }
		},
		FormatCase{
			.name = "R8_UNORM",
			.format = DXGI_FORMAT_R8_UNORM,
			.producerValue = { 1.0F / 255.0F, 0.0F, 0.0F, 0.0F },
			.consumerValue = { 2.0F / 255.0F, 0.0F, 0.0F, 0.0F }
		}
	};

	bool Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
		}
		return a_condition;
	}

	bool CheckHr(
		HRESULT a_result,
		std::string_view a_operation,
		std::string_view a_detail = {})
	{
		if (SUCCEEDED(a_result)) {
			return true;
		}
		std::cerr << "FAIL: " << a_operation;
		if (!a_detail.empty()) {
			std::cerr << " (" << a_detail << ')';
		}
		std::cerr << " hr=0x" << std::hex << std::uppercase <<
			static_cast<std::uint32_t>(a_result) << std::dec << '\n';
		return false;
	}

	const char* FeatureLevelName(D3D_FEATURE_LEVEL a_level)
	{
		switch (a_level) {
		case D3D_FEATURE_LEVEL_12_1:
			return "12_1";
		case D3D_FEATURE_LEVEL_12_0:
			return "12_0";
		case D3D_FEATURE_LEVEL_11_1:
			return "11_1";
		case D3D_FEATURE_LEVEL_11_0:
			return "11_0";
		default:
			return "unknown";
		}
	}

	D3D12_RESOURCE_BARRIER Transition(
		ID3D12Resource* a_resource,
		D3D12_RESOURCE_STATES a_before,
		D3D12_RESOURCE_STATES a_after)
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = a_resource;
		barrier.Transition.StateBefore = a_before;
		barrier.Transition.StateAfter = a_after;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		return barrier;
	}

	bool WaitForD3D11(
		ID3D11DeviceContext* a_context,
		ID3D11Query* a_query,
		std::chrono::milliseconds a_timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			const HRESULT result = a_context->GetData(
				a_query, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (result == S_OK) {
				return true;
			}
			if (FAILED(result)) {
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	bool OpenFence(
		ID3D12Device* a_device12,
		ID3D11Device5* a_device11,
		std::uint64_t a_initialValue,
		winrt::com_ptr<ID3D12Fence>& a_fence12,
		winrt::com_ptr<ID3D11Fence>& a_fence11)
	{
		if (FAILED(a_device12->CreateFence(
				a_initialValue,
				D3D12_FENCE_FLAG_SHARED,
				IID_PPV_ARGS(a_fence12.put())))) {
			return false;
		}
		HANDLE handle = nullptr;
		if (FAILED(a_device12->CreateSharedHandle(
				a_fence12.get(),
				nullptr,
				GENERIC_ALL,
				nullptr,
				&handle))) {
			return false;
		}
		const HRESULT result = a_device11->OpenSharedFence(
			handle, IID_PPV_ARGS(a_fence11.put()));
		CloseHandle(handle);
		return SUCCEEDED(result);
	}

	bool CreateSharedTexture(
		ID3D11Device5* a_device11,
		ID3D12Device* a_device12,
		const D3D11_TEXTURE2D_DESC& a_desc,
		const D3D11_SUBRESOURCE_DATA* a_initialData,
		std::string_view a_label,
		SharedTexture& a_texture)
	{
		auto desc = a_desc;
		desc.MiscFlags |=
			D3D11_RESOURCE_MISC_SHARED |
			D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		if (!CheckHr(
				a_device11->CreateTexture2D(
					&desc, a_initialData, a_texture.texture11.put()),
				"CreateTexture2D", a_label)) {
			return false;
		}

		winrt::com_ptr<IDXGIResource1> dxgiResource;
		if (!CheckHr(
				a_texture.texture11->QueryInterface(
					IID_PPV_ARGS(dxgiResource.put())),
				"QueryInterface(IDXGIResource1)", a_label)) {
			return false;
		}
		HANDLE handle = nullptr;
		if (!CheckHr(
				dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&handle),
				"CreateSharedHandle", a_label)) {
			return false;
		}
		const HRESULT openResult = a_device12->OpenSharedHandle(
			handle, IID_PPV_ARGS(a_texture.resource12.put()));
		CloseHandle(handle);
		return CheckHr(openResult, "OpenSharedHandle", a_label);
	}

	bool CreateReadback(
		ID3D12Device* a_device,
		ID3D12Resource* a_source,
		std::string_view a_label,
		Readback12& a_readback)
	{
		const auto sourceDesc = a_source->GetDesc();
		a_device->GetCopyableFootprints(
			&sourceDesc,
			0,
			1,
			0,
			&a_readback.footprint,
			nullptr,
			nullptr,
			&a_readback.size);
		const D3D12_HEAP_PROPERTIES heap{
			.Type = D3D12_HEAP_TYPE_READBACK
		};
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = a_readback.size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		return CheckHr(
			a_device->CreateCommittedResource(
				&heap,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(a_readback.resource.put())),
			"CreateCommittedResource(readback)", a_label);
	}

	bool CreateCommandRecording(
		ID3D12Device* a_device,
		std::string_view a_label,
		CommandRecording& a_recording)
	{
		if (!CheckHr(
				a_device->CreateCommandAllocator(
					D3D12_COMMAND_LIST_TYPE_DIRECT,
					IID_PPV_ARGS(a_recording.allocator.put())),
				"CreateCommandAllocator", a_label)) {
			return false;
		}
		return CheckHr(
			a_device->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				a_recording.allocator.get(),
				nullptr,
				IID_PPV_ARGS(a_recording.list.put())),
			"CreateCommandList", a_label);
	}

	template <class T>
	void AppendBytes(std::vector<std::byte>& a_bytes, T a_value)
	{
		const auto valueBytes =
			std::bit_cast<std::array<std::byte, sizeof(T)>>(a_value);
		a_bytes.insert(a_bytes.end(), valueBytes.begin(), valueBytes.end());
	}

	std::vector<std::byte> ExpectedBytes(
		DXGI_FORMAT a_format,
		bool a_producer)
	{
		std::vector<std::byte> bytes;
		switch (a_format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			if (a_producer) {
				return {
					std::byte{ 0 },
					std::byte{ 255 },
					std::byte{ 0 },
					std::byte{ 255 }
				};
			}
			return {
				std::byte{ 255 },
				std::byte{ 0 },
				std::byte{ 255 },
				std::byte{ 255 }
			};
		case DXGI_FORMAT_R32_FLOAT:
			AppendBytes(bytes, a_producer ? 0.25F : 0.75F);
			return bytes;
		case DXGI_FORMAT_R16G16_FLOAT:
			AppendBytes<std::uint16_t>(bytes, a_producer ? 0x3400 : 0x3A00);
			AppendBytes<std::uint16_t>(bytes, a_producer ? 0x3800 : 0x3000);
			return bytes;
		case DXGI_FORMAT_R8_UNORM:
			return { a_producer ? std::byte{ 1 } : std::byte{ 2 } };
		default:
			return {};
		}
	}

	void PrintBytes(
		std::string_view a_label,
		const std::vector<std::byte>& a_bytes)
	{
		std::cerr << a_label << '=';
		for (std::size_t index = 0; index < a_bytes.size(); ++index) {
			if (index != 0) {
				std::cerr << ',';
			}
			std::cerr << std::to_integer<unsigned>(a_bytes[index]);
		}
		std::cerr << '\n';
	}

	bool CheckFormatSupport(
		ID3D11Device5* a_device11,
		ID3D12Device* a_device12,
		const FormatCase& a_case)
	{
		UINT support11 = 0;
		if (!CheckHr(
				a_device11->CheckFormatSupport(a_case.format, &support11),
				"ID3D11Device::CheckFormatSupport", a_case.name)) {
			return false;
		}
		constexpr UINT required11 =
			D3D11_FORMAT_SUPPORT_TEXTURE2D |
			D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
			D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
		if ((support11 & required11) != required11) {
			std::cerr << "FAIL: " << a_case.name <<
				" lacks required D3D11 SRV/UAV support; support=0x" <<
				std::hex << support11 << std::dec << '\n';
			return false;
		}

		D3D12_FEATURE_DATA_FORMAT_SUPPORT support12{
			.Format = a_case.format
		};
		if (!CheckHr(
				a_device12->CheckFeatureSupport(
					D3D12_FEATURE_FORMAT_SUPPORT,
					&support12,
					sizeof(support12)),
				"ID3D12Device::CheckFeatureSupport(format)", a_case.name)) {
			return false;
		}
		constexpr D3D12_FORMAT_SUPPORT1 required12_1 =
			D3D12_FORMAT_SUPPORT1_TEXTURE2D |
			D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
		constexpr D3D12_FORMAT_SUPPORT2 required12_2 =
			D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
		if ((support12.Support1 & required12_1) != required12_1 ||
			(support12.Support2 & required12_2) != required12_2) {
			std::cerr << "FAIL: " << a_case.name <<
				" lacks required D3D12 SRV/UAV support; support1=0x" <<
				std::hex << support12.Support1 << " support2=0x" <<
				support12.Support2 << std::dec << '\n';
			return false;
		}
		return true;
	}

	bool RunBidirectionalAliasScenario(
		ID3D11Device5* a_device11,
		ID3D11DeviceContext4* a_context11,
		ID3D12Device* a_device12,
		ID3D12CommandQueue* a_queue,
		const FormatCase& a_case)
	{
		if (!CheckFormatSupport(a_device11, a_device12, a_case)) {
			return false;
		}
		const auto producerExpected = ExpectedBytes(a_case.format, true);
		const auto consumerExpected = ExpectedBytes(a_case.format, false);
		if (producerExpected.empty() || consumerExpected.empty()) {
			std::cerr << "FAIL: missing sentinel encoding for " << a_case.name << '\n';
			return false;
		}

		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = 1;
		textureDesc.Height = 1;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = a_case.format;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags =
			D3D11_BIND_SHADER_RESOURCE |
			D3D11_BIND_UNORDERED_ACCESS;
		SharedTexture texture;
		if (!CreateSharedTexture(
				a_device11,
				a_device12,
				textureDesc,
				nullptr,
				a_case.name,
				texture)) {
			return false;
		}

		winrt::com_ptr<ID3D11ShaderResourceView> srv11;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav11;
		if (!CheckHr(
				a_device11->CreateShaderResourceView(
					texture.texture11.get(), nullptr, srv11.put()),
				"CreateShaderResourceView(D3D11)", a_case.name) ||
			!CheckHr(
				a_device11->CreateUnorderedAccessView(
					texture.texture11.get(), nullptr, uav11.put()),
				"CreateUnorderedAccessView(D3D11)", a_case.name)) {
			return false;
		}
		const auto resourceDesc = texture.resource12->GetDesc();
		if ((resourceDesc.Flags &
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
			std::cerr << "FAIL: imported " << a_case.name <<
				" allocation lacks D3D12 UAV capability\n";
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC shaderHeapDesc{};
		shaderHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		shaderHeapDesc.NumDescriptors = 2;
		shaderHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		winrt::com_ptr<ID3D12DescriptorHeap> shaderDescriptorHeap;
		if (!CheckHr(
				a_device12->CreateDescriptorHeap(
					&shaderHeapDesc,
					IID_PPV_ARGS(shaderDescriptorHeap.put())),
				"CreateDescriptorHeap(shader-visible)", a_case.name)) {
			return false;
		}
		D3D12_DESCRIPTOR_HEAP_DESC cpuHeapDesc{};
		cpuHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cpuHeapDesc.NumDescriptors = 1;
		winrt::com_ptr<ID3D12DescriptorHeap> cpuDescriptorHeap;
		if (!CheckHr(
				a_device12->CreateDescriptorHeap(
					&cpuHeapDesc,
					IID_PPV_ARGS(cpuDescriptorHeap.put())),
				"CreateDescriptorHeap(CPU-only)", a_case.name)) {
			return false;
		}
		const auto descriptorSize = a_device12->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const auto srvCpu =
			shaderDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		const auto srvGpu =
			shaderDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE uavShaderCpu{
			.ptr = srvCpu.ptr + descriptorSize
		};
		D3D12_GPU_DESCRIPTOR_HANDLE uavGpu{
			.ptr = srvGpu.ptr + descriptorSize
		};
		const auto uavCpu =
			cpuDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = a_case.format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping =
			D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		a_device12->CreateShaderResourceView(
			texture.resource12.get(), &srvDesc, srvCpu);
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = a_case.format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		a_device12->CreateUnorderedAccessView(
			texture.resource12.get(), nullptr, &uavDesc, uavShaderCpu);
		a_device12->CreateUnorderedAccessView(
			texture.resource12.get(), nullptr, &uavDesc, uavCpu);

		Readback12 producerObservation;
		if (!CreateReadback(
				a_device12,
				texture.resource12.get(),
				a_case.name,
				producerObservation)) {
			return false;
		}
		CommandRecording recording;
		if (!CreateCommandRecording(a_device12, a_case.name, recording)) {
			return false;
		}
		winrt::com_ptr<ID3D12Fence> handoff12;
		winrt::com_ptr<ID3D11Fence> handoff11;
		if (!OpenFence(
				a_device12,
				a_device11,
				0,
				handoff12,
				handoff11)) {
			std::cerr << "FAIL: could not create shared handoff fence for " <<
				a_case.name << '\n';
			return false;
		}

		a_context11->ClearUnorderedAccessViewFloat(
			uav11.get(), a_case.producerValue.data());
		if (!CheckHr(
				a_context11->Signal(handoff11.get(), 1),
				"ID3D11DeviceContext4::Signal", a_case.name)) {
			return false;
		}
		a_context11->Flush();
		if (!CheckHr(
				a_queue->Wait(handoff12.get(), 1),
				"ID3D12CommandQueue::Wait", a_case.name)) {
			return false;
		}

		auto barrier = Transition(
			texture.resource12.get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_SOURCE);
		recording.list->ResourceBarrier(1, &barrier);
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = texture.resource12.get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = producerObservation.resource.get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		destination.PlacedFootprint = producerObservation.footprint;
		recording.list->CopyTextureRegion(
			&destination, 0, 0, 0, &source, nullptr);
		barrier = Transition(
			texture.resource12.get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		recording.list->ResourceBarrier(1, &barrier);
		ID3D12DescriptorHeap* heaps[]{ shaderDescriptorHeap.get() };
		recording.list->SetDescriptorHeaps(1, heaps);
		recording.list->ClearUnorderedAccessViewFloat(
			uavGpu,
			uavCpu,
			texture.resource12.get(),
			a_case.consumerValue.data(),
			0,
			nullptr);
		barrier = Transition(
			texture.resource12.get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COMMON);
		recording.list->ResourceBarrier(1, &barrier);
		if (!CheckHr(recording.list->Close(), "ID3D12GraphicsCommandList::Close", a_case.name)) {
			return false;
		}
		ID3D12CommandList* lists[]{ recording.list.get() };
		a_queue->ExecuteCommandLists(1, lists);
		if (!CheckHr(
				a_queue->Signal(handoff12.get(), 2),
				"ID3D12CommandQueue::Signal", a_case.name) ||
			!CheckHr(
				a_context11->Wait(handoff11.get(), 2),
				"ID3D11DeviceContext4::Wait", a_case.name)) {
			return false;
		}

		auto stagingDesc = textureDesc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;
		winrt::com_ptr<ID3D11Texture2D> consumerObservation;
		if (!CheckHr(
				a_device11->CreateTexture2D(
					&stagingDesc, nullptr, consumerObservation.put()),
				"CreateTexture2D(D3D11 readback)", a_case.name)) {
			return false;
		}
		const D3D11_QUERY_DESC queryDesc{ .Query = D3D11_QUERY_EVENT };
		winrt::com_ptr<ID3D11Query> observationDone;
		if (!CheckHr(
				a_device11->CreateQuery(&queryDesc, observationDone.put()),
				"CreateQuery", a_case.name)) {
			return false;
		}
		a_context11->CopyResource(
			consumerObservation.get(), texture.texture11.get());
		a_context11->End(observationDone.get());
		a_context11->Flush();
		if (!WaitForD3D11(
				a_context11,
				observationDone.get(),
				std::chrono::seconds(5))) {
			std::cerr << "FAIL: final D3D11 observation timed out for " <<
				a_case.name << '\n';
			return false;
		}

		std::vector<std::byte> observedProducer(producerExpected.size());
		void* mapped12 = nullptr;
		const D3D12_RANGE readRange{
			.Begin = static_cast<SIZE_T>(producerObservation.footprint.Offset),
			.End = static_cast<SIZE_T>(
				producerObservation.footprint.Offset + observedProducer.size())
		};
		if (!CheckHr(
				producerObservation.resource->Map(0, &readRange, &mapped12),
				"ID3D12Resource::Map", a_case.name)) {
			return false;
		}
		std::memcpy(
			observedProducer.data(),
			static_cast<const std::byte*>(mapped12) +
				producerObservation.footprint.Offset,
			observedProducer.size());
		const D3D12_RANGE emptyRange{};
		producerObservation.resource->Unmap(0, &emptyRange);

		std::vector<std::byte> observedConsumer(consumerExpected.size());
		D3D11_MAPPED_SUBRESOURCE mapped11{};
		if (!CheckHr(
				a_context11->Map(
					consumerObservation.get(),
					0,
					D3D11_MAP_READ,
					0,
					&mapped11),
				"ID3D11DeviceContext::Map", a_case.name)) {
			return false;
		}
		std::memcpy(
			observedConsumer.data(),
			mapped11.pData,
			observedConsumer.size());
		a_context11->Unmap(consumerObservation.get(), 0);

		bool ok = true;
		if (observedProducer != producerExpected) {
			std::cerr << "FAIL: D3D12 did not read the D3D11 sentinel for " <<
				a_case.name << '\n';
			PrintBytes("expected", producerExpected);
			PrintBytes("observed", observedProducer);
			ok = false;
		}
		if (observedConsumer != consumerExpected) {
			std::cerr << "FAIL: D3D11 did not read the D3D12 sentinel for " <<
				a_case.name << '\n';
			PrintBytes("expected", consumerExpected);
			PrintBytes("observed", observedConsumer);
			ok = false;
		}
		if (ok) {
			std::cout << "PASS: same-allocation bidirectional handoff " <<
				a_case.name << '\n';
		}
		return ok;
	}

	bool RunRetirementScenario(
		ID3D11Device5* a_device11,
		ID3D11DeviceContext4* a_context11,
		ID3D12Device* a_device12,
		ID3D12CommandQueue* a_presentingQueue,
		CompletionStrategy a_strategy)
	{
		constexpr std::array<std::uint8_t, 4> oldPixel{
			17, 31, 47, 255
		};
		constexpr std::array<std::uint8_t, 4> newPixel{
			101, 127, 149, 255
		};

		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = 1;
		textureDesc.Height = 1;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		D3D11_SUBRESOURCE_DATA initialData{};
		initialData.pSysMem = oldPixel.data();
		initialData.SysMemPitch =
			static_cast<UINT>(oldPixel.size());
		SharedTexture texture;
		if (!CreateSharedTexture(
				a_device11,
				a_device12,
				textureDesc,
				&initialData,
				"retirement producer",
				texture)) {
			return false;
		}
		D3D11_TEXTURE2D_DESC updateDesc = textureDesc;
		D3D11_SUBRESOURCE_DATA updateData{};
		updateData.pSysMem = newPixel.data();
		updateData.SysMemPitch =
			static_cast<UINT>(newPixel.size());
		winrt::com_ptr<ID3D11Texture2D> updateTexture;
		if (FAILED(a_device11->CreateTexture2D(
				&updateDesc, &updateData, updateTexture.put()))) {
			return Check(false, "could not create the producer update texture");
		}

		Readback12 readback;
		if (!CreateReadback(
				a_device12,
				texture.resource12.get(),
				"retirement producer",
				readback)) {
			return false;
		}
		CommandRecording recording;
		if (!CreateCommandRecording(
				a_device12, "retirement producer", recording)) {
			return false;
		}
		auto barrier = Transition(
			texture.resource12.get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_SOURCE);
		recording.list->ResourceBarrier(1, &barrier);
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = texture.resource12.get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = readback.resource.get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		destination.PlacedFootprint = readback.footprint;
		recording.list->CopyTextureRegion(
			&destination, 0, 0, 0, &source, nullptr);
		barrier = Transition(
			texture.resource12.get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_COMMON);
		recording.list->ResourceBarrier(1, &barrier);
		if (FAILED(recording.list->Close())) {
			return Check(false, "could not close the D3D12 copy command list");
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		winrt::com_ptr<ID3D12CommandQueue> consumerQueue;
		if (!CheckHr(
				a_device12->CreateCommandQueue(
					&queueDesc, IID_PPV_ARGS(consumerQueue.put())),
				"ID3D12Device::CreateCommandQueue",
				"delayed retirement consumer")) {
			return false;
		}
		winrt::com_ptr<ID3D12Fence> blocker;
		if (FAILED(a_device12->CreateFence(
				0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(blocker.put())))) {
			return Check(false, "could not create the delayed-completion fence");
		}
		winrt::com_ptr<ID3D12Fence> retirement12;
		winrt::com_ptr<ID3D11Fence> retirement11;
		if (!OpenFence(
				a_device12,
				a_device11,
				0,
				retirement12,
				retirement11)) {
			return Check(false, "could not create the retirement fence");
		}
		winrt::com_ptr<ID3D12Fence> consumerCompletion;
		if (FAILED(a_device12->CreateFence(
				0,
				D3D12_FENCE_FLAG_NONE,
				IID_PPV_ARGS(consumerCompletion.put())))) {
			return Check(false, "could not create the consumer completion fence");
		}
		winrt::com_ptr<ID3D12Fence> wrongFence;
		if (FAILED(a_device12->CreateFence(
				2, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(wrongFence.put())))) {
			return Check(false, "could not create the negative-control fence");
		}

		winrt::com_ptr<ID3D11Query> producerDone;
		const D3D11_QUERY_DESC queryDesc{ .Query = D3D11_QUERY_EVENT };
		winrt::handle consumerDone{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
		if (!consumerDone ||
			FAILED(a_device11->CreateQuery(&queryDesc, producerDone.put()))) {
			return Check(false, "could not create the completion observers");
		}
		if (FAILED(a_context11->Signal(retirement11.get(), 1)) ||
			FAILED(consumerQueue->Wait(retirement12.get(), 1))) {
			return Check(false, "could not publish the initialized producer texture");
		}
		a_context11->Flush();
		if (FAILED(consumerQueue->Wait(blocker.get(), 1))) {
			return Check(false, "could not delay the consumer queue");
		}
		ID3D12CommandList* lists[]{ recording.list.get() };
		consumerQueue->ExecuteCommandLists(1, lists);
		if (FAILED(consumerQueue->Signal(consumerCompletion.get(), 1))) {
			return Check(false, "could not publish delayed consumer completion");
		}

		using cs::render::temporal::GpuCompletionDependency;
		using cs::render::temporal::JoinPresentInputCompletion;
		if (a_strategy == CompletionStrategy::kQueueOrdered) {
			GpuCompletionDependency noDependency;
			GpuCompletionDependency zeroVendorValue{
				.fence = consumerCompletion
			};
			GpuCompletionDependency foreignPresentingQueue;
			foreignPresentingQueue.orderedQueue.copy_from(a_presentingQueue);
			if (!Check(
					JoinPresentInputCompletion(
						a_presentingQueue, noDependency) == E_INVALIDARG,
					"missing input completion dependency was accepted") ||
				!Check(
					JoinPresentInputCompletion(
						a_presentingQueue, zeroVendorValue) == E_INVALIDARG,
					"zero-valued vendor completion dependency was accepted") ||
				!Check(
					JoinPresentInputCompletion(
						consumerQueue.get(), foreignPresentingQueue) ==
						E_INVALIDARG,
					"queue-ordered dependency accepted the wrong presenting queue")) {
				return false;
			}
		}

		const bool ordered = [&] {
			GpuCompletionDependency dependency;
			switch (a_strategy) {
			case CompletionStrategy::kVendorFence:
				dependency.fence = consumerCompletion;
				dependency.value = 1;
				break;
			case CompletionStrategy::kQueueOrdered:
				// Model DLSS-G placing its last reader ahead of subsequent work
				// on the queue used to create presentation.
				if (FAILED(a_presentingQueue->Wait(
						consumerCompletion.get(), 1))) {
					return Check(
						false,
						"could not order the delayed reader onto the presenting queue");
				}
				dependency.orderedQueue.copy_from(a_presentingQueue);
				break;
			case CompletionStrategy::kWrongFence:
				dependency.fence = wrongFence;
				dependency.value = 2;
				break;
			}
			if (FAILED(JoinPresentInputCompletion(
					a_presentingQueue, dependency))) {
				return Check(false, "could not join input completion");
			}
			if (FAILED(a_presentingQueue->Signal(retirement12.get(), 2))) {
				return Check(false, "could not signal input retirement");
			}
			if (FAILED(a_context11->Wait(retirement11.get(), 2))) {
				return Check(false, "could not queue the D3D11 producer wait");
			}
			a_context11->CopyResource(texture.texture11.get(), updateTexture.get());
			a_context11->End(producerDone.get());
			a_context11->Flush();
			const bool shouldPreserveOldInput =
				a_strategy != CompletionStrategy::kWrongFence;
			const bool completedWhileConsumerBlocked = WaitForD3D11(
				a_context11, producerDone.get(),
				std::chrono::milliseconds(
					shouldPreserveOldInput ? 100 : 5000));
			return Check(
				completedWhileConsumerBlocked != shouldPreserveOldInput,
				shouldPreserveOldInput
					? "producer overwrite completed before the consumer retirement signal"
					: "wrong-fence negative control did not bypass the real consumer");
		}();

		if (FAILED(blocker->Signal(1))) {
			return Check(false, "could not release the delayed consumer");
		}
		a_context11->Flush();
		if (FAILED(consumerCompletion->SetEventOnCompletion(
				1, consumerDone.get())) ||
			WaitForSingleObject(consumerDone.get(), 5000) != WAIT_OBJECT_0) {
			return Check(false, "consumer did not retire before readback and resource release");
		}
		if (!ordered) {
			return false;
		}
		if (!WaitForD3D11(
				a_context11,
				producerDone.get(),
				std::chrono::seconds(5))) {
			return Check(false, "producer did not complete after retirement");
		}

		std::array<std::uint8_t, 4> consumed{};
		void* mapped = nullptr;
		D3D12_RANGE readRange{
			.Begin = static_cast<SIZE_T>(readback.footprint.Offset),
			.End = static_cast<SIZE_T>(
				readback.footprint.Offset + oldPixel.size())
		};
		if (FAILED(readback.resource->Map(0, &readRange, &mapped))) {
			return Check(false, "could not map the D3D12 readback");
		}
		std::memcpy(
			consumed.data(),
			static_cast<const std::byte*>(mapped) + readback.footprint.Offset,
			consumed.size());
		const D3D12_RANGE emptyRange{};
		readback.resource->Unmap(0, &emptyRange);
		const bool shouldPreserveOldInput =
			a_strategy != CompletionStrategy::kWrongFence;
		const bool expectedResult = shouldPreserveOldInput
			? consumed == oldPixel
			: consumed != oldPixel;
		if (!expectedResult) {
			std::cerr << "observed pixel=" <<
				static_cast<unsigned>(consumed[0]) << ',' <<
				static_cast<unsigned>(consumed[1]) << ',' <<
				static_cast<unsigned>(consumed[2]) << ',' <<
				static_cast<unsigned>(consumed[3]) << '\n';
		}
		return Check(
			expectedResult,
			shouldPreserveOldInput
				? "retired reuse did not preserve the consumer's old input"
				: "wrong-fence negative control unexpectedly preserved the old input");
	}

	bool SameLuid(const LUID& a_left, const LUID& a_right)
	{
		return a_left.HighPart == a_right.HighPart &&
			a_left.LowPart == a_right.LowPart;
	}

	bool TryCreateDevices(
		IDXGIAdapter1* a_adapter,
		bool a_reportFailure,
		const std::filesystem::path* a_agilitySdkDirectory,
		DeviceBundle& a_devices)
	{
		DeviceBundle candidate;
		candidate.adapter.copy_from(a_adapter);
		HRESULT result = a_agilitySdkDirectory
			? cs::features::CreatePrivateD3D12Device(
				  a_adapter,
				  *a_agilitySdkDirectory,
				  candidate.deviceFactory,
				  candidate.device12.put(),
				  &candidate.agility)
			: D3D12CreateDevice(
				  a_adapter,
				  D3D_FEATURE_LEVEL_11_0,
				  IID_PPV_ARGS(candidate.device12.put()));
		if (FAILED(result)) {
			if (a_reportFailure) {
				CheckHr(result,
					a_agilitySdkDirectory
						? "CreatePrivateD3D12Device"
						: "D3D12CreateDevice");
			}
			return false;
		}
		if (a_agilitySdkDirectory &&
			!candidate.agility.UsedSdkFactory()) {
			if (a_reportFailure) {
				std::cerr <<
					"FAIL: Agility SDK factory fell back to the "
					"system runtime status=" <<
					cs::features::AgilityBootstrapStatusName(
						candidate.agility.status) <<
					" activation=0x" << std::hex <<
					std::uppercase <<
					static_cast<std::uint32_t>(
						candidate.agility.activationResult) <<
					std::dec << '\n';
			}
			return false;
		}
		if (a_agilitySdkDirectory &&
			(candidate.agility.sdkDirectory.empty() ||
				!candidate.agility.sdkDirectory.is_absolute() ||
				candidate.agility.loadedD3D12Core.empty() ||
				candidate.agility.packagedVersion.major != 1 ||
				candidate.agility.packagedVersion.minor != 616 ||
				candidate.agility.packagedVersion.patch != 1 ||
				candidate.agility.loadedVersion.major == 0 ||
				!candidate.deviceFactory)) {
			if (a_reportFailure) {
				Check(false,
					"SDK device factory did not report an absolute SDK "
					"path and an actual loaded D3D12Core identity");
			}
			return false;
		}
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		result = candidate.device12->CreateCommandQueue(
			&queueDesc, IID_PPV_ARGS(candidate.queue.put()));
		if (FAILED(result)) {
			if (a_reportFailure) {
				CheckHr(result, "ID3D12Device::CreateCommandQueue");
			}
			return false;
		}

		winrt::com_ptr<ID3D11Device> baseDevice11;
		winrt::com_ptr<ID3D11DeviceContext> baseContext11;
		result = D3D11CreateDevice(
			a_adapter,
			D3D_DRIVER_TYPE_UNKNOWN,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			kRequestedFeatureLevels.data(),
			static_cast<UINT>(kRequestedFeatureLevels.size()),
			D3D11_SDK_VERSION,
			baseDevice11.put(),
			&candidate.featureLevel11,
			baseContext11.put());
		if (FAILED(result)) {
			if (a_reportFailure) {
				CheckHr(result, "D3D11CreateDevice");
			}
			return false;
		}
		result = baseDevice11->QueryInterface(
			IID_PPV_ARGS(candidate.device11.put()));
		if (SUCCEEDED(result)) {
			result = baseContext11->QueryInterface(
				IID_PPV_ARGS(candidate.context11.put()));
		}
		if (FAILED(result)) {
			if (a_reportFailure) {
				CheckHr(result, "shared-fence D3D11 interfaces");
			}
			return false;
		}

		D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels12{
			.NumFeatureLevels =
				static_cast<UINT>(kRequestedFeatureLevels.size()),
			.pFeatureLevelsRequested = kRequestedFeatureLevels.data()
		};
		if (SUCCEEDED(candidate.device12->CheckFeatureSupport(
				D3D12_FEATURE_FEATURE_LEVELS,
				&featureLevels12,
				sizeof(featureLevels12)))) {
			candidate.featureLevel12 = featureLevels12.MaxSupportedFeatureLevel;
		}

		DXGI_ADAPTER_DESC1 selectedDesc{};
		a_adapter->GetDesc1(&selectedDesc);
		const auto luid12 = candidate.device12->GetAdapterLuid();
		winrt::com_ptr<IDXGIDevice> dxgiDevice11;
		winrt::com_ptr<IDXGIAdapter> adapter11;
		DXGI_ADAPTER_DESC desc11{};
		if (FAILED(baseDevice11->QueryInterface(
				IID_PPV_ARGS(dxgiDevice11.put()))) ||
			FAILED(dxgiDevice11->GetAdapter(adapter11.put())) ||
			FAILED(adapter11->GetDesc(&desc11)) ||
			!SameLuid(selectedDesc.AdapterLuid, luid12) ||
			!SameLuid(selectedDesc.AdapterLuid, desc11.AdapterLuid)) {
			if (a_reportFailure) {
				Check(false, "D3D11 and D3D12 devices are not on the selected adapter LUID");
			}
			return false;
		}

		a_devices = std::move(candidate);
		return true;
	}

	bool SelectDevices(
		IDXGIFactory6* a_factory,
		bool a_hardware,
		const std::filesystem::path* a_agilitySdkDirectory,
		bool a_reportHardwareUnavailable,
		DeviceBundle& a_devices)
	{
		if (!a_hardware) {
			winrt::com_ptr<IDXGIAdapter1> warp;
			if (!CheckHr(
				a_factory->EnumWarpAdapter(IID_PPV_ARGS(warp.put())),
				"IDXGIFactory::EnumWarpAdapter")) {
				return false;
			}
			return TryCreateDevices(
				warp.get(), true, a_agilitySdkDirectory, a_devices);
		}

		for (UINT index = 0;; ++index) {
			winrt::com_ptr<IDXGIAdapter1> adapter;
			const HRESULT result = a_factory->EnumAdapterByGpuPreference(
				index,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(adapter.put()));
			if (result == DXGI_ERROR_NOT_FOUND) {
				break;
			}
			if (FAILED(result)) {
				return CheckHr(result, "IDXGIFactory6::EnumAdapterByGpuPreference");
			}
			DXGI_ADAPTER_DESC1 desc{};
			if (FAILED(adapter->GetDesc1(&desc)) ||
				(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
				continue;
			}
			if (TryCreateDevices(
					adapter.get(), false, a_agilitySdkDirectory,
					a_devices)) {
				return true;
			}
		}
		return !a_reportHardwareUnavailable
			? false
			: Check(false,
				  "no hardware adapter supports the required same-LUID "
				  "D3D11/D3D12 devices");
	}

	void PrintDeviceIdentity(
		const DeviceBundle& a_devices,
		bool a_hardware)
	{
		DXGI_ADAPTER_DESC1 desc{};
		a_devices.adapter->GetDesc1(&desc);
		std::wcout << L"Adapter mode=" <<
			(a_hardware ? L"hardware" : L"WARP") <<
			L" name=\"" << desc.Description << L"\" vendor=0x" <<
			std::hex << desc.VendorId << L" device=0x" << desc.DeviceId <<
			L" luid=" << static_cast<std::uint32_t>(desc.AdapterLuid.HighPart) <<
			L':' << desc.AdapterLuid.LowPart << std::dec << L'\n';
		std::cout << "Feature levels: D3D11=" <<
			FeatureLevelName(a_devices.featureLevel11) <<
			" D3D12=" << FeatureLevelName(a_devices.featureLevel12) << '\n';
		if (a_devices.agility.UsedSdkFactory()) {
			const auto& version = a_devices.agility.loadedVersion;
			const auto& packaged =
				a_devices.agility.packagedVersion;
			std::wcout << L"SDK " <<
				cs::features::kPrivateD3D12SdkVersion <<
				L" device factory active; requested path=\"" <<
				a_devices.agility.sdkDirectory.wstring() <<
				L"\" package version=" << packaged.major << L'.' <<
				packaged.minor << L'.' << packaged.patch << L'.' <<
				packaged.revision << L"; selected core source=" <<
				(a_devices.agility.LoadedPackagedCore()
					? L"packaged"
					: L"system/other") <<
				L" path=\"" <<
				a_devices.agility.loadedD3D12Core.wstring() <<
				L"\" version=" << version.major << L'.' <<
				version.minor << L'.' << version.patch << L'.' <<
				version.revision << L'\n';
		}
	}

	bool EnableDebugLayerIfAvailable()
	{
		winrt::com_ptr<ID3D12Debug> debug;
		const HRESULT result = D3D12GetDebugInterface(
			IID_PPV_ARGS(debug.put()));
		if (FAILED(result)) {
			std::cout << "D3D12 debug layer unavailable\n";
			return false;
		}
		debug->EnableDebugLayer();
		std::cout << "D3D12 debug layer enabled\n";
		return true;
	}

	bool CheckDebugMessages(ID3D12Device* a_device)
	{
		winrt::com_ptr<ID3D12InfoQueue> messages;
		if (!CheckHr(a_device->QueryInterface(IID_PPV_ARGS(messages.put())),
				"QueryInterface(ID3D12InfoQueue)")) {
			return false;
		}

		bool ok = true;
		const auto count = messages->GetNumStoredMessagesAllowedByRetrievalFilter();
		for (UINT64 index = 0; index < count; ++index) {
			SIZE_T size = 0;
			if (!CheckHr(messages->GetMessage(index, nullptr, &size),
					"GetMessage(size)")) {
				return false;
			}
			std::vector<std::byte> storage(size);
			auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
			if (!CheckHr(messages->GetMessage(index, message, &size),
					"GetMessage")) {
				return false;
			}
			if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
				message->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
				std::cerr << "FAIL: D3D12 validation " <<
					static_cast<unsigned>(message->ID) << ": " <<
					message->pDescription << '\n';
				ok = false;
			}
		}
		return ok;
	}
}

int main(int a_argc, char** a_argv)
{
	bool hardware = false;
	std::optional<std::filesystem::path> agilitySdkDirectory;
	if (a_argc == 2 && std::strcmp(a_argv[1], "--hardware") == 0) {
		hardware = true;
	} else if (a_argc == 3 &&
		(std::strcmp(a_argv[1], "--agility") == 0 ||
			std::strcmp(a_argv[1], "--agility-hardware") == 0)) {
		hardware =
			std::strcmp(a_argv[1], "--agility-hardware") == 0;
		agilitySdkDirectory =
			std::filesystem::path(a_argv[2]);
	} else if (a_argc != 1) {
		std::cerr <<
			"Usage: FrameGenerationRetirementGpuTests.exe "
			"[--hardware | --agility <sdk-directory> | "
			"--agility-hardware <sdk-directory>]\n";
		return 1;
	}

	const bool debugLayerEnabled =
		!agilitySdkDirectory && EnableDebugLayerIfAvailable();
	winrt::com_ptr<IDXGIFactory6> factory;
	if (!CheckHr(
			CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())),
			"CreateDXGIFactory2")) {
		return 1;
	}
	DeviceBundle devices;
	if (!SelectDevices(
			factory.get(),
			hardware,
			agilitySdkDirectory ? &*agilitySdkDirectory : nullptr,
			true,
			devices)) {
		return 1;
	}
	PrintDeviceIdentity(devices, hardware);

	bool ok = true;
	for (const auto& formatCase : kFormatCases) {
		ok &= RunBidirectionalAliasScenario(
			devices.device11.get(),
			devices.context11.get(),
			devices.device12.get(),
			devices.queue.get(),
			formatCase);
	}
	ok &= RunRetirementScenario(
		devices.device11.get(),
		devices.context11.get(),
		devices.device12.get(),
		devices.queue.get(),
		CompletionStrategy::kVendorFence);
	ok &= RunRetirementScenario(
		devices.device11.get(),
		devices.context11.get(),
		devices.device12.get(),
		devices.queue.get(),
		CompletionStrategy::kQueueOrdered);
	ok &= RunRetirementScenario(
		devices.device11.get(),
		devices.context11.get(),
		devices.device12.get(),
		devices.queue.get(),
		CompletionStrategy::kWrongFence);
	if (debugLayerEnabled) {
		ok &= CheckDebugMessages(devices.device12.get());
	}
	if (!ok) {
		return 1;
	}
	std::cout << "Frame-generation GPU retirement tests passed\n";
	return 0;
}
