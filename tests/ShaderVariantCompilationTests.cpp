#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>

#include "Render/ShaderVariantCompilation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
	using namespace cs::engine;
	using namespace std::chrono_literals;

	int failures = 0;

	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::printf("FAIL: %s\n", a_message);
			++failures;
		}
	}

	winrt::com_ptr<ID3D11Device> CreateWarpDevice()
	{
		winrt::com_ptr<ID3D11Device> device;
		D3D_FEATURE_LEVEL featureLevel{};
		const std::array featureLevels{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0
		};
		const auto result = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			0,
			featureLevels.data(),
			static_cast<UINT>(featureLevels.size()),
			D3D11_SDK_VERSION,
			device.put(),
			&featureLevel,
			nullptr);
		return SUCCEEDED(result) ? device : nullptr;
	}

	winrt::com_ptr<ID3D11DeviceChild> CreateDeviceChild(
		ID3D11Device& a_device)
	{
		D3D11_BUFFER_DESC descriptor{};
		descriptor.ByteWidth = 16;
		descriptor.Usage = D3D11_USAGE_DEFAULT;
		descriptor.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		winrt::com_ptr<ID3D11Buffer> buffer;
		if (FAILED(a_device.CreateBuffer(
				&descriptor, nullptr, buffer.put()))) {
			return {};
		}
		winrt::com_ptr<ID3D11DeviceChild> child;
		child.attach(buffer.detach());
		return child;
	}

	ShaderVariantCompilationRequest Request(ID3D11Device& a_device)
	{
		ShaderVariantCompilationRequest request;
		request.device.copy_from(&a_device);
		request.sourcePath = L"Data/Shaders/Probe.hlsl";
		request.entryPoint = "main";
		request.profile = "ps_5_0";
		request.stage = ShaderStage::kPixel;
		request.familyId = 7;
		request.descriptor = 42;
		request.owner = "ProbeFamily";
		request.defines = {
			{ "FAMILY", "7" },
			{ "TEXTURE", "1" }
		};
		return request;
	}

	bool WaitForState(
		const std::shared_ptr<ShaderVariantCompilationHandle>& a_handle,
		ShaderVariantCompilationState a_state)
	{
		const auto deadline = std::chrono::steady_clock::now() + 2s;
		while (std::chrono::steady_clock::now() < deadline) {
			if (a_handle->GetState() == a_state)
				return true;
			std::this_thread::sleep_for(1ms);
		}
		return a_handle->GetState() == a_state;
	}

	void CheckCoalescedNonblockingReady(ID3D11Device& a_device)
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool entered = false;
		bool release = false;
		std::atomic<unsigned> attempts{ 0 };
		auto child = CreateDeviceChild(a_device);
		auto cache =
			CreateAsyncShaderVariantCompilationCache(
				[&](ShaderVariantCompilationRequest request) {
					attempts.fetch_add(1, std::memory_order_relaxed);
					{
						std::unique_lock lock(mutex);
						entered = true;
						condition.notify_all();
						condition.wait(lock, [&] { return release; });
					}
					(void)request;
					return ShaderVariantCompilationOutput{
						.shader = child
					};
				},
				2);

		constexpr std::size_t requesterCount = 12;
		std::barrier start(static_cast<std::ptrdiff_t>(requesterCount));
		std::array<
			std::shared_ptr<ShaderVariantCompilationHandle>,
			requesterCount>
			handles;
		std::vector<std::jthread> requesters;
		requesters.reserve(requesterCount);
		for (std::size_t index = 0; index < requesterCount; ++index) {
			requesters.emplace_back([&, index] {
				start.arrive_and_wait();
				handles[index] = cache->Request(Request(a_device));
			});
		}
		requesters.clear();

		{
			std::unique_lock lock(mutex);
			Check(
				condition.wait_for(lock, 1s, [&] { return entered; }),
				"blocked compilation worker did not start");
		}
		Check(
			std::ranges::all_of(
				handles,
				[&](const auto& handle) {
					return handle == handles.front();
				}),
			"concurrent requests did not coalesce to one handle");
		Check(
			attempts.load(std::memory_order_relaxed) == 1,
			"concurrent requests started more than one compile");
		Check(
			handles.front()->GetState()
					== ShaderVariantCompilationState::kPending
				&& !handles.front()->Acquire(),
			"pending compilation blocked or returned a shader");

		{
			std::scoped_lock lock(mutex);
			release = true;
		}
		condition.notify_all();
		Check(
			WaitForState(
				handles.front(),
				ShaderVariantCompilationState::kReady),
			"completed compilation did not transition to ready");
		Check(
			!!handles.front()->Acquire(),
			"ready compilation did not publish its shader");
	}

	void CheckFailureAndInvalidation(ID3D11Device& a_device)
	{
		std::atomic<unsigned> attempts{ 0 };
		std::atomic<unsigned> completionCalls{ 0 };
		std::atomic<unsigned> failedCompletions{ 0 };
		std::atomic<unsigned> readyCompletions{ 0 };
		std::array<std::uint64_t, 2> generations{};
		auto child = CreateDeviceChild(a_device);
		auto cache =
			CreateAsyncShaderVariantCompilationCache(
				[&](ShaderVariantCompilationRequest request) {
					const auto attempt =
						attempts.fetch_add(1, std::memory_order_relaxed);
					if (attempt < generations.size())
						generations[attempt] = request.sourceGeneration;
					ShaderVariantCompilationOutput result;
					if (attempt == 0) {
						result.error = "controlled compiler failure";
					} else {
						result.shader = child;
					}
					return result;
				});

		auto failedRequest = Request(a_device);
		failedRequest.completion = [&](
			ShaderVariantCompilationState a_state,
			std::string_view a_error) {
			if (a_state == ShaderVariantCompilationState::kFailed
				&& a_error == "controlled compiler failure") {
				failedCompletions.fetch_add(
					1, std::memory_order_relaxed);
			}
			completionCalls.fetch_add(1, std::memory_order_release);
		};
		const auto failed = cache->Request(std::move(failedRequest));
		Check(
			WaitForState(failed, ShaderVariantCompilationState::kFailed),
			"failed compilation did not become terminal");
		const auto waitForCompletionCount = [&](unsigned a_expected) {
			const auto deadline =
				std::chrono::steady_clock::now() + 2s;
			while (std::chrono::steady_clock::now() < deadline) {
				if (completionCalls.load(std::memory_order_acquire)
					>= a_expected) {
					return true;
				}
				std::this_thread::sleep_for(1ms);
			}
			return completionCalls.load(std::memory_order_acquire)
				>= a_expected;
		};
		Check(
			waitForCompletionCount(1)
				&& failedCompletions.load(std::memory_order_relaxed) == 1,
			"compiler failure did not notify its completion observer");
		const auto repeated = cache->Request(Request(a_device));
		Check(
			repeated == failed
				&& attempts.load(std::memory_order_relaxed) == 1
				&& completionCalls.load(std::memory_order_relaxed) == 1
				&& failed->GetError() == "controlled compiler failure",
			"terminal failure was retried, re-notified, or lost its diagnostic");

		cache->Invalidate();
		auto retryRequest = Request(a_device);
		retryRequest.completion = [&](
			ShaderVariantCompilationState a_state,
			std::string_view) {
			if (a_state == ShaderVariantCompilationState::kReady) {
				readyCompletions.fetch_add(
					1, std::memory_order_relaxed);
			}
			completionCalls.fetch_add(1, std::memory_order_release);
		};
		const auto retried = cache->Request(std::move(retryRequest));
		Check(
			retried != failed,
			"invalidation reused the terminal failure handle");
		Check(
			WaitForState(retried, ShaderVariantCompilationState::kReady)
				&& waitForCompletionCount(2)
				&& attempts.load(std::memory_order_relaxed) == 2
				&& readyCompletions.load(std::memory_order_relaxed) == 1
				&& generations[1] > generations[0],
			"invalidation did not permit exactly one fresh generation attempt");
	}

	void CheckCompilerExceptions(ID3D11Device& a_device)
	{
		for (const bool standardException : { true, false }) {
			std::atomic<unsigned> attempts{ 0 };
			auto child = CreateDeviceChild(a_device);
			auto cache = CreateAsyncShaderVariantCompilationCache(
				[&](ShaderVariantCompilationRequest request) {
					++attempts;
					if (request.descriptor == 42) {
						if (standardException)
							throw std::runtime_error("compiler exception");
						throw 42;
					}
					return ShaderVariantCompilationOutput{ .shader = child };
				});
			const auto failed = cache->Request(Request(a_device));
			Check(
				WaitForState(failed, ShaderVariantCompilationState::kFailed)
					&& !failed->GetError().empty(),
				"compiler exception did not publish a failed diagnostic");
			Check(
				cache->Request(Request(a_device)) == failed && attempts == 1,
				"compiler exception was retried");
			auto next = Request(a_device);
			++next.descriptor;
			Check(
				WaitForState(
					cache->Request(std::move(next)),
					ShaderVariantCompilationState::kReady),
				"compiler exception stopped the worker from processing later work");
		}
	}

	void CheckSafeShutdown(ID3D11Device& a_device)
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool entered = false;
		bool release = false;
		auto cache =
			CreateAsyncShaderVariantCompilationCache(
				[&](ShaderVariantCompilationRequest) {
					std::unique_lock lock(mutex);
					entered = true;
					condition.notify_all();
					condition.wait(lock, [&] { return release; });
					ShaderVariantCompilationOutput result;
					result.error = "stopped worker result";
					return result;
				});
		const auto inflight = cache->Request(Request(a_device));
		const auto queuedRequest = [&] {
			auto request = Request(a_device);
			request.defines.push_back({ "SECOND", "1" });
			return request;
		}();
		const auto queued = cache->Request(queuedRequest);
		{
			std::unique_lock lock(mutex);
			Check(
				condition.wait_for(lock, 1s, [&] { return entered; }),
				"shutdown fixture worker did not start");
		}

		auto stopping = std::async(
			std::launch::async,
			[&] { cache->Stop(); });
		Check(
			stopping.wait_for(25ms) == std::future_status::timeout,
			"shutdown did not wait for the in-flight worker");
		Check(
			WaitForState(queued, ShaderVariantCompilationState::kFailed),
			"shutdown did not fail a queued request");
		{
			std::scoped_lock lock(mutex);
			release = true;
		}
		condition.notify_all();
		Check(
			stopping.wait_for(1s) == std::future_status::ready,
			"worker shutdown did not complete safely");
		Check(
			WaitForState(inflight, ShaderVariantCompilationState::kFailed),
			"in-flight result published after shutdown");
	}
}

int main()
{
	const auto device = CreateWarpDevice();
	Check(!!device, "could not create WARP device");
	if (!device)
		return 1;

	CheckCoalescedNonblockingReady(*device);
	CheckFailureAndInvalidation(*device);
	CheckCompilerExceptions(*device);
	CheckSafeShutdown(*device);
	return failures == 0 ? 0 : 1;
}
