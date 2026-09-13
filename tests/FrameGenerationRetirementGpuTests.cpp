#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

namespace
{
	bool Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
		}
		return a_condition;
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

	bool RunRetirementScenario(
		ID3D11Device5* a_device11,
		ID3D11DeviceContext4* a_context11,
		ID3D12Device* a_device12,
		ID3D12CommandQueue* a_queue,
		bool a_useCorrectFence)
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
		textureDesc.MiscFlags =
			D3D11_RESOURCE_MISC_SHARED |
			D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		D3D11_SUBRESOURCE_DATA initialData{};
		initialData.pSysMem = oldPixel.data();
		initialData.SysMemPitch =
			static_cast<UINT>(oldPixel.size());
		winrt::com_ptr<ID3D11Texture2D> texture11;
		if (FAILED(a_device11->CreateTexture2D(
				&textureDesc, &initialData, texture11.put()))) {
			return Check(false, "could not create the shared producer texture");
		}

		winrt::com_ptr<IDXGIResource1> dxgiResource;
		if (FAILED(texture11->QueryInterface(
				IID_PPV_ARGS(dxgiResource.put())))) {
			return Check(false, "could not query the shared texture handle");
		}
		HANDLE textureHandle = nullptr;
		if (FAILED(dxgiResource->CreateSharedHandle(
				nullptr,
				DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
				nullptr,
				&textureHandle))) {
			return Check(false, "could not export the shared producer texture");
		}
		winrt::com_ptr<ID3D12Resource> texture12;
		const HRESULT openTextureResult = a_device12->OpenSharedHandle(
			textureHandle, IID_PPV_ARGS(texture12.put()));
		CloseHandle(textureHandle);
		if (FAILED(openTextureResult)) {
			return Check(false, "could not import the producer texture into D3D12");
		}
		D3D11_TEXTURE2D_DESC updateDesc = textureDesc;
		updateDesc.MiscFlags = 0;
		D3D11_SUBRESOURCE_DATA updateData{};
		updateData.pSysMem = newPixel.data();
		updateData.SysMemPitch =
			static_cast<UINT>(newPixel.size());
		winrt::com_ptr<ID3D11Texture2D> updateTexture;
		if (FAILED(a_device11->CreateTexture2D(
				&updateDesc, &updateData, updateTexture.put()))) {
			return Check(false, "could not create the producer update texture");
		}

		D3D12_RESOURCE_DESC sourceDesc = texture12->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT64 readbackSize = 0;
		a_device12->GetCopyableFootprints(
			&sourceDesc,
			0,
			1,
			0,
			&footprint,
			nullptr,
			nullptr,
			&readbackSize);
		const D3D12_HEAP_PROPERTIES readbackHeap{
			.Type = D3D12_HEAP_TYPE_READBACK
		};
		D3D12_RESOURCE_DESC readbackDesc{};
		readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		readbackDesc.Width = readbackSize;
		readbackDesc.Height = 1;
		readbackDesc.DepthOrArraySize = 1;
		readbackDesc.MipLevels = 1;
		readbackDesc.SampleDesc.Count = 1;
		readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		winrt::com_ptr<ID3D12Resource> readback;
		if (FAILED(a_device12->CreateCommittedResource(
				&readbackHeap,
				D3D12_HEAP_FLAG_NONE,
				&readbackDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(readback.put())))) {
			return Check(false, "could not create the D3D12 readback buffer");
		}

		winrt::com_ptr<ID3D12CommandAllocator> allocator;
		winrt::com_ptr<ID3D12GraphicsCommandList> commandList;
		if (FAILED(a_device12->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(allocator.put()))) ||
			FAILED(a_device12->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				allocator.get(),
				nullptr,
				IID_PPV_ARGS(commandList.put())))) {
			return Check(false, "could not create the D3D12 copy command list");
		}
		D3D12_RESOURCE_BARRIER before{};
		before.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		before.Transition.pResource = texture12.get();
		before.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		before.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		before.Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &before);
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = texture12.get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = readback.get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		destination.PlacedFootprint = footprint;
		commandList->CopyTextureRegion(
			&destination, 0, 0, 0, &source, nullptr);
		D3D12_RESOURCE_BARRIER after{};
		after.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		after.Transition.pResource = texture12.get();
		after.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		after.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
		after.Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &after);
		if (FAILED(commandList->Close())) {
			return Check(false, "could not close the D3D12 copy command list");
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
		winrt::com_ptr<ID3D12Fence> wrong12;
		winrt::com_ptr<ID3D11Fence> wrong11;
		if (!OpenFence(
				a_device12,
				a_device11,
				2,
				wrong12,
				wrong11)) {
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
			FAILED(a_queue->Wait(retirement12.get(), 1))) {
			return Check(false, "could not publish the initialized producer texture");
		}
		if (FAILED(a_queue->Wait(blocker.get(), 1))) {
			return Check(false, "could not delay the consumer queue");
		}
		ID3D12CommandList* lists[]{ commandList.get() };
		a_queue->ExecuteCommandLists(1, lists);
		const bool ordered = [&] {
			if (FAILED(a_queue->Signal(retirement12.get(), 2))) {
				return Check(false, "could not signal input retirement");
			}
			auto* waitFence =
				a_useCorrectFence ? retirement11.get() : wrong11.get();
			if (FAILED(a_context11->Wait(waitFence, 2))) {
				return Check(false, "could not queue the D3D11 producer wait");
			}
			a_context11->CopyResource(texture11.get(), updateTexture.get());
			a_context11->End(producerDone.get());
			a_context11->Flush();
			const bool completedWhileConsumerBlocked = WaitForD3D11(
				a_context11, producerDone.get(),
				std::chrono::milliseconds(a_useCorrectFence ? 100 : 5000));
			return Check(
				completedWhileConsumerBlocked != a_useCorrectFence,
				a_useCorrectFence
					? "producer overwrite completed before the consumer retirement signal"
					: "wrong-fence negative control did not bypass the real consumer");
		}();

		if (FAILED(blocker->Signal(1))) {
			return Check(false, "could not release the delayed consumer");
		}
		a_context11->Flush();
		// The negative control's producer query does not depend on consumer completion.
		if (FAILED(a_queue->Signal(retirement12.get(), 3)) ||
			FAILED(retirement12->SetEventOnCompletion(3, consumerDone.get())) ||
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
			.Begin = footprint.Offset,
			.End = footprint.Offset + oldPixel.size()
		};
		if (FAILED(readback->Map(0, &readRange, &mapped))) {
			return Check(false, "could not map the D3D12 readback");
		}
		std::memcpy(
			consumed.data(),
			static_cast<const std::byte*>(mapped) + footprint.Offset,
			consumed.size());
		const D3D12_RANGE emptyRange{};
		readback->Unmap(0, &emptyRange);
		const bool expectedResult = a_useCorrectFence
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
			a_useCorrectFence
				? "retired reuse did not preserve the consumer's old input"
				: "wrong-fence negative control unexpectedly preserved the old input");
	}
}

int main()
{
	winrt::com_ptr<IDXGIFactory4> factory;
	if (FAILED(CreateDXGIFactory2(
			0, IID_PPV_ARGS(factory.put())))) {
		Check(false, "could not create DXGI factory");
		return 1;
	}
	winrt::com_ptr<IDXGIAdapter> adapter;
	if (FAILED(factory->EnumWarpAdapter(
			IID_PPV_ARGS(adapter.put())))) {
		Check(false, "could not enumerate the WARP adapter");
		return 1;
	}
	winrt::com_ptr<ID3D12Device> device12;
	if (FAILED(D3D12CreateDevice(
			adapter.get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(device12.put())))) {
		Check(false, "could not create the WARP D3D12 device");
		return 1;
	}
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	winrt::com_ptr<ID3D12CommandQueue> queue;
	if (FAILED(device12->CreateCommandQueue(
			&queueDesc, IID_PPV_ARGS(queue.put())))) {
		Check(false, "could not create the WARP D3D12 queue");
		return 1;
	}

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	winrt::com_ptr<ID3D11Device> baseDevice11;
	winrt::com_ptr<ID3D11DeviceContext> baseContext11;
	if (FAILED(D3D11CreateDevice(
			adapter.get(),
			D3D_DRIVER_TYPE_UNKNOWN,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			&featureLevel,
			1,
			D3D11_SDK_VERSION,
			baseDevice11.put(),
			nullptr,
			baseContext11.put()))) {
		Check(false, "could not create the WARP D3D11 device");
		return 1;
	}
	winrt::com_ptr<ID3D11Device5> device11;
	winrt::com_ptr<ID3D11DeviceContext4> context11;
	if (FAILED(baseDevice11->QueryInterface(
			IID_PPV_ARGS(device11.put()))) ||
		FAILED(baseContext11->QueryInterface(
			IID_PPV_ARGS(context11.put())))) {
		Check(false, "WARP does not expose shared-fence D3D11 interfaces");
		return 1;
	}

	bool ok = RunRetirementScenario(
		device11.get(),
		context11.get(),
		device12.get(),
		queue.get(),
		true);
	ok &= RunRetirementScenario(
		device11.get(),
		context11.get(),
		device12.get(),
		queue.get(),
		false);
	if (!ok) {
		return 1;
	}
	std::cout << "Frame-generation GPU retirement tests passed\n";
	return 0;
}
