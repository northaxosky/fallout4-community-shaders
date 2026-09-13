#include "Render/FrameGenerationCpuTiming.h"
#include "Render/FrameGenerationOrchestration.h"
#include "Render/TemporalPipelineState.h"

#pragma warning(push)
#pragma warning(disable: 4068 4100)
#include "FidelityFXFrameGenerationContract.h"
#pragma warning(pop)

#include "StreamlineFrameGenerationContract.h"

#pragma warning(push)
#pragma warning(disable: 4068 4100)
#include <FidelityFX/api/include/ffx_api.hpp>
#pragma warning(pop)

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	int failures = 0;
	std::array<std::uint64_t, 8> fakeClockValues{};
	std::size_t fakeClockIndex = 0;

	std::uint64_t FakeClock() noexcept { return fakeClockValues[fakeClockIndex++]; }

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	void TestFailureReporting()
	{
		using namespace cs::render::temporal;
		const ProviderResult failure{ .code = ProviderResultCode::kFailure,
			.sdkResult = -12,
			.message = "Frame interpolation failed." };
		const auto message = FormatProviderFailure("Collect present status", failure);
		Check(message ==
				  "Collect present status: Frame interpolation failed. (SDK "
				  "result -12)",
			"failure reason preserves its operation and exact signed SDK code");
	}

	void TestCpuPhaseTimingCollector()
	{
		using Phase = cs::render::FrameGenerationCpuPhase;
		cs::render::FrameGenerationCpuTimingCollector<3> collector(&FakeClock);

		fakeClockIndex = 0;
		{
			auto scope = collector.Measure(Phase::kLatencySleep);
		}
		Check(fakeClockIndex == 0 && !collector.GetSnapshot().available,
			"disabled CPU phase timing does not invoke the clock");

		collector.SetEnabled(true);
		collector.RecordNanoseconds(Phase::kLatencySleep, 1'000'000);
		collector.RecordNanoseconds(Phase::kLatencySleep, 2'000'000);
		collector.RecordNanoseconds(Phase::kLatencySleep, 3'000'000);
		collector.RecordNanoseconds(Phase::kLatencySleep, 4'000'000);
		collector.RecordNanoseconds(Phase::kSdkPresent, 8'000'000);
		collector.RecordFrameTimeInput(15.5);
		auto snapshot = collector.GetSnapshot();
		const auto& sleep =
			snapshot.phases[static_cast<std::size_t>(Phase::kLatencySleep)];
		const auto& present =
			snapshot.phases[static_cast<std::size_t>(Phase::kSdkPresent)];
		Check(snapshot.available && sleep.sampleCount == 4 &&
				  sleep.windowSampleCount == 3 &&
				  sleep.windowMeanMilliseconds == 3.0 &&
				  sleep.windowMaxMilliseconds == 4.0,
			"CPU phase timing keeps a deterministic bounded rolling window");
		Check(present.sampleCount == 1 && present.windowMeanMilliseconds == 8.0 &&
				  snapshot.frameTimeInputAvailable &&
				  snapshot.lastFrameTimeInputMilliseconds == 15.5 &&
				  snapshot.phases[static_cast<std::size_t>(Phase::kPrepareFrame)]
						  .sampleCount == 0,
			"CPU phase timing keeps stages independent");

		collector.SetEnabled(false);
		collector.SetEnabled(true);
		fakeClockValues = { 10, 2'000'010 };
		fakeClockIndex = 0;
		const auto earlyReturn = [&]() {
			auto scope = collector.Measure(Phase::kPrepareFrame);
			return;
		};
		earlyReturn();
		snapshot = collector.GetSnapshot();
		const auto& prepare =
			snapshot.phases[static_cast<std::size_t>(Phase::kPrepareFrame)];
		Check(fakeClockIndex == 2 && prepare.sampleCount == 1 &&
				  prepare.windowMeanMilliseconds == 2.0,
			"CPU phase timing records scopes that leave through an early return");

		collector.SetEnabled(false);
		fakeClockIndex = 0;
		{
			auto scope = collector.Measure(Phase::kSdkPresent);
		}
		snapshot = collector.GetSnapshot();
		Check(fakeClockIndex == 0 && !snapshot.available &&
				  !snapshot.frameTimeInputAvailable &&
				  snapshot.phases[static_cast<std::size_t>(Phase::kSdkPresent)]
						  .sampleCount == 0,
			"disabling CPU phase timing clears samples and reports unavailable");
	}

	class RecordingProvider final : public cs::render::temporal::IFrameGenerationProvider
	{
	public:
		const char* Name() const noexcept override { return "recording"; }
		cs::render::temporal::ProviderResult PrepareDevice(ID3D12Device**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult
		PrepareFactory(IDXGIFactory4**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult
		CreatePresentation(const cs::render::temporal::PresentationCreateContext&,
			IDXGISwapChain4**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult
		CreateDisplayResources(std::uint32_t a_width, std::uint32_t a_height,
			DXGI_FORMAT, std::uint32_t) override
		{
			events.emplace_back("create:" + std::to_string(a_width) + "x" +
								std::to_string(a_height));
			return createSucceeds ? Success() : Failure("create");
		}
		cs::render::temporal::ProviderResult
		PrepareFrame(const cs::render::temporal::FrameGenerationRequest&) override
		{
			events.emplace_back("prepare");
			return prepareSucceeds ? Success() : Failure("prepare");
		}
		cs::render::temporal::ProviderResult
		CancelFrame(const cs::render::temporal::FrameGenerationRequest&) override
		{
			events.emplace_back("cancel");
			return cancelSucceeds ? Success() : Failure("cancel");
		}
		cs::render::temporal::ProviderResult
		SetGenerationEnabled(bool a_enabled) override
		{
			events.emplace_back(a_enabled ? "enable" : "disable");
			return Success();
		}
		cs::render::temporal::PresentInputRetirementMode
		GetPresentInputRetirementMode() const noexcept override
		{
			return cs::render::temporal::PresentInputRetirementMode::
				kRecordedCommandList;
		}
		cs::render::temporal::ProviderResult
		CollectPresentStatus(UINT a_flags, HRESULT a_result) override
		{
			++statusCalls;
			lastStatusFlags = a_flags;
			lastPresentResult = a_result;
			return Success();
		}
		std::optional<std::uint32_t> ConsumeGeneratedFrameCount() noexcept override
		{
			return generatedCount;
		}
		std::optional<std::uint32_t> ConsumePresentedFrameCount() noexcept override
		{
			return presentedCount;
		}
		cs::render::temporal::ProviderResult Sleep(std::uint32_t) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult
		SetLatencyMarker(cs::render::temporal::LatencyMarker,
			std::uint32_t) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult Quiesce() override
		{
			events.emplace_back("quiesce");
			return quiesceSucceeds ? Success() : Failure("quiesce");
		}
		cs::render::temporal::ProviderResult
		ReleaseDisplayResources() noexcept override
		{
			events.emplace_back("release");
			auto result = releaseSucceeds ? Success() : Failure("release");
			result.globalDrainAttempted = releaseGlobalDrainAttempted;
			result.globalDrainCompleted = releaseGlobalDrainCompleted;
			return result;
		}
		cs::render::temporal::ProviderResult DestroyAfterDrain() noexcept override
		{
			events.emplace_back("destroy");
			return Success();
		}
		bool IsReady() const noexcept override { return true; }

		static cs::render::temporal::ProviderResult Success()
		{
			return { .code = cs::render::temporal::ProviderResultCode::kSuccess };
		}
		static cs::render::temporal::ProviderResult Failure(std::string a_message)
		{
			return { .code = cs::render::temporal::ProviderResultCode::kFailure,
				.message = std::move(a_message) };
		}

		std::vector<std::string> events;
		bool prepareSucceeds = true;
		bool cancelSucceeds = true;
		bool quiesceSucceeds = true;
		bool releaseSucceeds = true;
		bool releaseGlobalDrainAttempted = true;
		bool releaseGlobalDrainCompleted = true;
		bool createSucceeds = true;
		std::uint32_t statusCalls = 0;
		UINT lastStatusFlags = 0;
		HRESULT lastPresentResult = E_FAIL;
		std::optional<std::uint32_t> generatedCount;
		std::optional<std::uint32_t> presentedCount;
	};

	void TestLifecycleOrderAndFailures()
	{
		RecordingProvider provider;
		const auto result =
			cs::render::temporal::QuiesceDrainAndRelease(provider, [&]() {
				provider.events.emplace_back("drain");
				return true;
			});
		Check(result.Succeeded(), "successful lifecycle completes");
		Check(result.globalDrainAttempted && result.globalDrainCompleted,
			"successful lifecycle reports the provider drain that completed");
		Check(provider.events ==
				  std::vector<std::string>{ "quiesce", "drain", "release" },
			"provider disable/quiesce precedes CPU drain and release");

		provider.events.clear();
		const auto drainFailure =
			cs::render::temporal::QuiesceDrainAndRelease(provider, [&]() {
				provider.events.emplace_back("drain");
				return false;
			});
		Check(!drainFailure.Succeeded(), "drain failure is visible");
		Check(provider.events == std::vector<std::string>{ "quiesce", "drain" },
			"drain failure never frees provider resources");

		provider.events.clear();
		provider.quiesceSucceeds = false;
		const auto quiesceFailure =
			cs::render::temporal::QuiesceDrainAndRelease(provider, [&]() {
				provider.events.emplace_back("drain");
				return true;
			});
		Check(!quiesceFailure.Succeeded(), "quiesce failure is visible");
		Check(provider.events == std::vector<std::string>{ "quiesce" },
			"quiesce failure performs neither drain nor release");
	}

	void TestResizeRestoration()
	{
		using cs::render::temporal::ProviderDisplayDescription;
		RecordingProvider provider;
		const ProviderDisplayDescription oldDescription{
			.width = 1280,
			.height = 720,
			.format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.bufferCount = 2
		};
		const ProviderDisplayDescription changedDescription{
			.width = 1920,
			.height = 1080,
			.format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.bufferCount = 2
		};

		auto result = cs::render::temporal::RestoreProviderAfterResize(
			provider, S_OK, oldDescription, oldDescription);
		Check(result.Succeeded(), "same-size resize restores provider readiness");
		Check(provider.events.back() == "create:1280x720",
			"same-size resize recreates provider display resources");

		result = cs::render::temporal::RestoreProviderAfterResize(
			provider, S_OK, oldDescription, changedDescription);
		Check(result.Succeeded(), "changed-size resize restores provider readiness");
		Check(provider.events.back() == "create:1920x1080",
			"changed-size resize uses the actual new description");

		const HRESULT rejected = DXGI_ERROR_INVALID_CALL;
		const auto restoration =
			cs::render::temporal::RestoreProviderAndPreserveResizeResult(
				provider, rejected, oldDescription, std::nullopt);
		Check(restoration.providerResult.Succeeded(),
			"rejected resize attempts old-provider restoration");
		Check(provider.events.back() == "create:1280x720",
			"rejected resize restores from the retained description");
		Check(restoration.nativeResizeResult == rejected,
			"provider restoration preserves the rejected native resize HRESULT");

		provider.createSucceeds = false;
		result = cs::render::temporal::RestoreProviderAfterResize(
			provider, S_OK, oldDescription, oldDescription);
		Check(!result.Succeeded(),
			"provider recreation failure remains visible for quarantine");
		Check(
			cs::render::temporal::CompleteNativeResize(S_OK, S_FALSE) == S_OK,
			"provider-only recreation failure preserves a successful native resize");
		Check(cs::render::temporal::CompleteNativeResize(S_OK, E_OUTOFMEMORY) ==
				  E_OUTOFMEMORY,
			"bridge-resource failure publishes an explicit invalid resize result");
	}

	void TestTransactionalResourceReplacement()
	{
		using cs::render::temporal::CommitAfterGpuDrain;
		struct Tracked
		{
			std::vector<std::string>* events;
			std::string name;
			~Tracked() { events->push_back("destroy-" + name); }
		};

		std::vector<std::string> events;
		auto current = std::make_unique<Tracked>(&events, "old");
		auto replacement = std::make_unique<Tracked>(&events, "new");
		const auto result = CommitAfterGpuDrain(
			true,
			[&] {
				events.emplace_back("drain");
				return S_OK;
			},
			[&] {
				events.emplace_back("commit");
				current = std::move(replacement);
			});
		Check(result == S_OK && current && current->name == "new" &&
				  events.size() >= 3 && events[0] == "drain" &&
				  events[1] == "commit" && events[2] == "destroy-old",
			"bridge replacement drains before releasing old resources");

		auto retained = current.get();
		Check(CommitAfterGpuDrain(
				  true, [] { return E_FAIL; }, [&] { current.reset(); }) == E_FAIL &&
				  current.get() == retained,
			"failed drain preserves the complete prior resource set");
		bool touched = false;
		Check(CommitAfterGpuDrain(
				  false,
				  [&] {
					  touched = true;
					  return S_OK;
				  },
				  [&] { touched = true; }) == E_FAIL &&
				  !touched && current.get() == retained,
			"incomplete replacement construction preserves old resources without "
			"draining");
	}

	void TestResizeCommitProtocol()
	{
		DXGI_SWAP_CHAIN_DESC proxy{};
		proxy.BufferDesc.Width = 1280;
		proxy.BufferDesc.Height = 720;
		proxy.BufferCount = 2;
		DXGI_SWAP_CHAIN_DESC1 inner{};
		inner.Width = 1280;
		inner.Height = 720;
		inner.BufferCount = 2;
		std::array<std::uint64_t, 2> fenceValues{ 7, 9 };
		cs::render::temporal::PresentInputReuseGate gate;
		gate.MarkSubmitted(1, cs::render::temporal::PresentInputRetirementToken{
								  .value = 5,
								  .realFrame = 4,
								  .resourceGeneration = 2,
								  .queueIdentity = 9 });
		std::uint32_t frameSlot = 1;
		bool presentPrepared = true;
		bool preparedGeneration = true;
		bool vendorConsumption = true;
		bool preparedTransaction = true;
		const DXGI_SWAP_CHAIN_DESC1 resized{
			.Width = 1920,
			.Height = 1080,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc = { 1, 0 },
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 2,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
			.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
		};
		cs::render::temporal::CommitResizeBridgeState(
			resized, proxy, inner, fenceValues, gate, frameSlot, presentPrepared,
			preparedGeneration, vendorConsumption, preparedTransaction);
		Check(proxy.BufferDesc.Width == 1920 && proxy.BufferDesc.Height == 1080 &&
				  proxy.BufferCount == 2 && proxy.Flags == 0 && inner.Width == 1920 &&
				  inner.Height == 1080,
			"successful native resize commits dimensions without leaking "
			"inner-only flags");
		Check(fenceValues == std::array<std::uint64_t, 2>{ 0, 0 } && frameSlot == 0 &&
				  !gate.IsPending(1) && !presentPrepared && !preparedGeneration &&
				  !vendorConsumption && !preparedTransaction,
			"resize commit invalidates prepared and retry protocol state");
		Check(cs::render::temporal::CompleteNativeResize(S_OK, S_FALSE) == S_OK &&
				  proxy.BufferDesc.Width == 1920 && inner.Width == 1920,
			"provider quarantine after commit cannot roll published metadata back "
			"to the old native size");
	}

	void TestSafePreparation()
	{
		RecordingProvider provider;
		cs::render::temporal::FrameGenerationRequest request;
		provider.prepareSucceeds = false;
		const auto safe = cs::render::temporal::PrepareFrameSafely(provider, request);
		Check(!safe.prepared && safe.safeToPresent,
			"failed preparation is safe only after cancellation");
		provider.events.emplace_back("present");
		Check(provider.events ==
				  std::vector<std::string>{ "prepare", "cancel", "present" },
			"SDK disable/cancellation precedes the same underlying Present");

		provider.events.clear();
		provider.cancelSucceeds = false;
		const auto unsafe =
			cs::render::temporal::PrepareFrameSafely(provider, request);
		if (unsafe.safeToPresent) {
			provider.events.emplace_back("present");
		}
		Check(!unsafe.safeToPresent,
			"failed cancellation blocks unsafe interpolation submission");
		Check(provider.events == std::vector<std::string>{ "prepare", "cancel" },
			"unsafe cancellation path does not call Present");
	}

	void TestInputReuseGate()
	{
		using namespace cs::render::temporal;
		cs::render::temporal::PresentInputReuseGate gate;
		gate.MarkSubmitted(0, PresentInputRetirementToken{ .value = 7,
								  .realFrame = 19,
								  .resourceGeneration = 3,
								  .queueIdentity = 11 });
		std::uint32_t acquisitions = 0;
		auto acquired =
			gate.Acquire(0, 3, 11, 4, [&](const PresentInputRetirementToken&) {
				++acquisitions;
				return S_OK;
			});
		Check(acquired.Succeeded() && acquired.waitRequired && acquisitions == 1 &&
				  ShouldPublishPresentInputAcquireTelemetry(acquired),
			"first delayed acquisition queues a wait and publishes its token");
		Check(!gate.IsPending(0), "queued GPU wait retires slot ownership");

		acquired =
			gate.Acquire(0, 3, 11, 4, [&](const PresentInputRetirementToken&) {
				++acquisitions;
				return S_OK;
			});
		Check(acquired.Succeeded() && !acquired.firstAcquire && acquisitions == 1 &&
				  !ShouldPublishPresentInputAcquireTelemetry(acquired),
			"repeated writes share one acquisition without clearing latest-token "
			"telemetry");

		gate.MarkSubmitted(1, PresentInputRetirementToken{ .value = 12,
								  .realFrame = 20,
								  .resourceGeneration = 3,
								  .queueIdentity = 11 });
		acquired =
			gate.Acquire(1, 3, 11, 12, [&](const PresentInputRetirementToken&) {
				++acquisitions;
				return S_OK;
			});
		Check(acquired.Succeeded() && !acquired.waitRequired && acquisitions == 1,
			"already completed retirement acquires without a queue wait");
	}

	void TestSynchronousPresentRetirement()
	{
		using namespace cs::render::temporal;
		std::vector<std::string> events;
		std::uint64_t nextFence = 4;
		const auto submission = PresentAndRetireInputs(
			PresentInputRetirementMode::kSynchronousPresentQueue, true, 90, 7, 13,
			nextFence,
			[&]() {
				events.emplace_back("sdk-last-read-submit");
				events.emplace_back("present-return");
				return S_OK;
			},
			[&](std::uint64_t a_value) {
				events.emplace_back("signal:" + std::to_string(a_value));
				return S_OK;
			});
		Check(submission.presentResult == S_OK && submission.signalResult == S_OK &&
				  submission.token && submission.token->value == 4 &&
				  submission.token->realFrame == 90 &&
				  submission.token->resourceGeneration == 7 &&
				  submission.token->queueIdentity == 13 && nextFence == 5,
			"synchronous Present emits an exact same-queue retirement token");
		Check(events == std::vector<std::string>{ "sdk-last-read-submit",
							"present-return", "signal:4" },
			"retirement signal is submitted after the SDK last reader");

		events.clear();
		const auto failed = PresentAndRetireInputs(
			PresentInputRetirementMode::kSynchronousPresentQueue, true, 91, 7, 13,
			nextFence,
			[&]() {
				events.emplace_back("present-return");
				return DXGI_ERROR_DEVICE_REMOVED;
			},
			[&](std::uint64_t) {
				events.emplace_back("signal-failed");
				return DXGI_ERROR_DEVICE_REMOVED;
			});
		Check(FAILED(failed.presentResult) && FAILED(failed.signalResult) &&
				  !failed.token &&
				  events ==
					  std::vector<std::string>{ "present-return", "signal-failed" },
			"device removal never publishes an unproven retirement token");

		events.clear();
		const auto disabled = PresentAndRetireInputs(
			PresentInputRetirementMode::kSynchronousPresentQueue, false, 92, 7, 13,
			nextFence,
			[&]() {
				events.emplace_back("present");
				return S_OK;
			},
			[&](std::uint64_t) {
				events.emplace_back("unexpected-signal");
				return S_OK;
			});
		Check(!disabled.token && events == std::vector<std::string>{ "present" },
			"disabled frames do not create borrowed-input ownership");
	}

	void TestDelayedRetirementAcrossRingCycles()
	{
		using namespace cs::render::temporal;
		PresentInputReuseGate gate;
		std::vector<std::string> events;
		std::uint64_t completed = 0;
		for (std::uint64_t frame = 0; frame < 8; ++frame) {
			const auto slot = static_cast<std::uint32_t>(frame % 2);
			const auto acquired = gate.Acquire(
				slot, 4, 77, completed,
				[&](const PresentInputRetirementToken& a_token) {
					events.emplace_back("wait:" + std::to_string(a_token.value));
					return S_OK;
				});
			Check(acquired.Succeeded(),
				"ring slot acquires after its exact retirement dependency");
			events.emplace_back("write:" + std::to_string(frame));
			gate.MarkSubmitted(slot,
				PresentInputRetirementToken{ .value = frame + 1,
					.realFrame = frame,
					.resourceGeneration = 4,
					.queueIdentity = 77 });
			completed = frame > 2 ? frame - 2 : 0;
		}
		Check(events == std::vector<std::string>{ "write:0", "write:1", "wait:1",
							"write:2", "wait:2", "write:3",
							"wait:3", "write:4", "wait:4",
							"write:5", "wait:5", "write:6",
							"wait:6", "write:7" },
			"multiple ring cycles always queue the prior slot token before "
			"overwrite");

		struct FailureCase
		{
			std::uint64_t tokenGeneration;
			std::uint64_t acquireGeneration;
			std::uint64_t tokenQueue;
			std::uint64_t acquireQueue;
			std::uint64_t completed;
			HRESULT waitResult;
			PresentInputAcquireCode code;
			HRESULT result;
		};
		for (const auto test : std::array{
				 FailureCase{ 4, 4, 77, 77, 10, E_ACCESSDENIED,
					 PresentInputAcquireCode::kWaitFailed, E_ACCESSDENIED },
				 FailureCase{ 3, 4, 77, 77, 30, S_OK,
					 PresentInputAcquireCode::kGenerationMismatch,
					 E_INVALIDARG },
				 FailureCase{ 4, 4, 77, 99, 30, S_OK,
					 PresentInputAcquireCode::kQueueMismatch, E_INVALIDARG },
				 FailureCase{ 4, 4, 77, 77, UINT64_MAX, S_OK,
					 PresentInputAcquireCode::kFenceUnavailable,
					 DXGI_ERROR_DEVICE_REMOVED } }) {
			PresentInputReuseGate failureGate;
			failureGate.MarkSubmitted(0, PresentInputRetirementToken{
											 .value = 30,
											 .realFrame = 12,
											 .resourceGeneration = test.tokenGeneration,
											 .queueIdentity = test.tokenQueue });
			const auto failed = failureGate.Acquire(
				0, test.acquireGeneration, test.acquireQueue, test.completed,
				[&](const PresentInputRetirementToken&) { return test.waitResult; });
			Check(failed.code == test.code && failed.result == test.result &&
					  failureGate.IsPending(0) &&
					  ShouldPublishPresentInputAcquireTelemetry(failed),
				"retirement validation preserves failed ownership and exact HRESULT");
		}
	}

	void TestPresentStatusOrchestration()
	{
		RecordingProvider provider;
		provider.generatedCount = 2;
		provider.presentedCount = 3;
		struct Case
		{
			UINT flags;
			HRESULT result;
			bool observed;
		};
		for (const auto test :
			std::array{ Case{ 0, S_OK, true }, Case{ DXGI_PRESENT_TEST, S_OK, false },
				Case{ 0, DXGI_ERROR_WAS_STILL_DRAWING, false },
				Case{ 0, DXGI_ERROR_DEVICE_REMOVED, false } }) {
			const auto priorCalls = provider.statusCalls;
			const auto collected = cs::render::temporal::CollectAcceptedPresentStatus(
				provider, test.flags, test.result);
			Check(collected.observed == test.observed &&
					  provider.statusCalls ==
						  priorCalls + static_cast<std::uint32_t>(test.observed),
				"only an accepted real Present polls provider status");
			if (test.observed) {
				Check(collected.generatedFrames == 2u &&
						  collected.presentedFrames == 3u &&
						  provider.lastStatusFlags == test.flags &&
						  provider.lastPresentResult == test.result,
					"accepted Present retains the provider counts and native result");
			}
		}
	}

	void TestStreamlineBackendContracts()
	{
		cs::render::temporal::PresentedFrameAccumulator counts;
		sl::DLSSGStatus status = sl::DLSSGStatus::eOk;
		std::uint32_t calls = 0;
		for (const std::uint32_t count : { 0u, 1u, 4u }) {
			const auto result = cs::features::streamline_fg::PollState(
				sl::ViewportHandle{ 1 }, counts, status,
				[&](sl::ViewportHandle, sl::DLSSGState& a_state,
					const sl::DLSSGOptions*) {
					++calls;
					a_state.numFramesActuallyPresented = count;
					a_state.status = sl::DLSSGStatus::eOk;
					return sl::Result::eOk;
				});
			Check(result == sl::Result::eOk, "DLSS-G state query succeeds");
		}
		Check(calls == 3 && counts.Consume() == 5,
			"the one DLSS-G state owner retains 0, 1, and multiple presentations");

		struct CleanupCase
		{
			bool allocated;
			int failingStep;
			bool succeeds;
			std::vector<std::string> events;
		};
		for (const auto& test :
			std::array{ CleanupCase{ false, -1, true, {} },
				CleanupCase{ true, -1, true, { "clear", "options", "free" } },
				CleanupCase{ true, 0, false, { "clear" } },
				CleanupCase{ true, 1, false, { "clear", "options" } },
				CleanupCase{ true, 2, false, { "clear", "options", "free" } } }) {
			std::vector<std::string> events;
			sl::DLSSGOptions options{};
			const auto stepResult = [&](int a_step) {
				return test.failingStep == a_step ? sl::Result::eErrorInvalidState : sl::Result::eOk;
			};
			const auto result = cs::features::streamline_fg::DestroyResources(
				test.allocated, sl::ViewportHandle{ 9 },
				[&]() {
					events.emplace_back("clear");
					return stepResult(0);
				},
				[&](sl::ViewportHandle, const sl::DLSSGOptions& a_options) {
					events.emplace_back("options");
					options = a_options;
					return stepResult(1);
				},
				[&](sl::Feature a_feature, sl::ViewportHandle) {
					events.emplace_back("free");
					Check(a_feature == sl::kFeatureDLSS_G,
						"DLSS-G cleanup frees only its own allocation");
					return stepResult(2);
				});
			Check(result.succeeded == test.succeeds && events == test.events,
				"DLSS-G cleanup stops at the first failed ownership step");
			if (test.allocated && test.failingStep != 0) {
				Check(options.mode == sl::DLSSGMode::eOff,
					"DLSS-G cleanup disables generation before resource free");
			}
		}
	}

	void TestFidelityFXBackendContracts()
	{
		Check(static_cast<std::uint32_t>(
				  ffx::ReturnCode::ErrorProviderNoSupportNewDesctype) == 7,
			"FidelityFX return code 7 remains named and numerically intact");
		ffx::QueryGetProviderVersion version;
		Check(version.header.type == FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION,
			"FidelityFX provider-version query uses the compiled descriptor type");

		auto* swapChain = reinterpret_cast<IDXGISwapChain4*>(0x1234);
		const auto config = cs::features::fidelityfx_fg::BuildDisabledConfiguration(
			swapChain, 88, 1920, 1080);
		Check(!config.frameGenerationEnabled &&
				  config.frameGenerationCallback == nullptr &&
				  config.frameGenerationCallbackUserContext == nullptr &&
				  config.HUDLessColor.resource == nullptr &&
				  config.presentCallback == nullptr &&
				  config.presentCallbackUserContext == nullptr &&
				  config.swapChain == swapChain && config.frameID == 88 &&
				  config.flags == 0 && !config.allowAsyncWorkloads &&
				  config.generationRect.width == 1920 &&
				  config.generationRect.height == 1080,
			"FSR deconfiguration detaches callbacks, user context, and HUD-less "
			"input before destruction");

		ffx::Context context{};
		const auto unsupported = cs::features::fidelityfx_fg::QueryProviderVersion(
			context, [](ffx::Context&, ffx::QueryGetProviderVersion&) {
				return ffx::ReturnCode::ErrorProviderNoSupportNewDesctype;
			});
		Check(!unsupported.available && unsupported.id == 0 &&
				  unsupported.name.empty() &&
				  unsupported.result ==
					  ffx::ReturnCode::ErrorProviderNoSupportNewDesctype,
			"unsupported FSR provider-version query is explicit and non-fatal");
	}

	void TestProductionInputWriteOrdering()
	{
		using namespace cs::render::temporal;
		cs::render::temporal::PresentInputReuseGate gate;
		gate.MarkSubmitted(1, PresentInputRetirementToken{ .value = 22,
								  .realFrame = 55,
								  .resourceGeneration = 8,
								  .queueIdentity = 6 });
		std::vector<std::string> events;
		auto write = [&](std::string a_name) {
			return cs::render::temporal::WriteFrameGenerationInput(
				[&]() {
					const auto result = gate.Acquire(
						1, 8, 6, 10, [&](const PresentInputRetirementToken& a_token) {
							events.emplace_back("gpu-wait:" +
												std::to_string(a_token.value));
							return S_OK;
						});
					return result.Succeeded();
				},
				[&]() { events.emplace_back(std::move(a_name)); });
		};
		Check(write("alpha"), "queued GPU wait permits ordered alpha input write");
		Check(write("raw"), "same slot permits raw input write");
		Check(write("hudless"), "same slot permits HUD-less input write");
		Check(events ==
				  std::vector<std::string>{ "gpu-wait:22", "alpha", "raw", "hudless" },
			"producer queue wait is ordered before the first write and runs once "
			"across all captures");
	}

	void TestDisableDrainDestroyOrder()
	{
		std::vector<std::string> events;
		const auto result = cs::render::temporal::DisableDrainAndDestroy(
			[&]() {
				events.emplace_back("disable-null-callbacks");
				return true;
			},
			[&]() {
				events.emplace_back("drain");
				return true;
			},
			[&]() {
				events.emplace_back("destroy-context");
				return true;
			});
		Check(result == cs::render::temporal::DestructionResult::kSuccess,
			"callback deconfiguration lifecycle succeeds");
		Check(events == std::vector<std::string>{ "disable-null-callbacks", "drain",
							"destroy-context" },
			"callbacks are detached before drain and context destruction");

		events.clear();
		const auto disableFailure = cs::render::temporal::DisableDrainAndDestroy(
			[&]() {
				events.emplace_back("disable-null-callbacks");
				return false;
			},
			[&]() {
				events.emplace_back("drain");
				return true;
			},
			[&]() {
				events.emplace_back("destroy-context");
				return true;
			});
		Check(disableFailure ==
					  cs::render::temporal::DestructionResult::kDisableFailed &&
				  events == std::vector<std::string>{ "disable-null-callbacks" },
			"failed callback deconfiguration prevents drain and destruction");
	}
}  // namespace

int main()
{
	TestCpuPhaseTimingCollector();
	TestFailureReporting();
	TestLifecycleOrderAndFailures();
	TestResizeRestoration();
	TestTransactionalResourceReplacement();
	TestResizeCommitProtocol();
	TestSafePreparation();
	TestInputReuseGate();
	TestSynchronousPresentRetirement();
	TestDelayedRetirementAcrossRingCycles();
	TestPresentStatusOrchestration();
	TestStreamlineBackendContracts();
	TestFidelityFXBackendContracts();
	TestProductionInputWriteOrdering();
	TestDisableDrainDestroyOrder();
	if (failures) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Frame-generation contract tests passed\n";
	return 0;
}
