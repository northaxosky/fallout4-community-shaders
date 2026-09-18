#include "Render/FrameGenerationCpuTiming.h"
#include "Render/FrameGenerationOrchestration.h"
#include "Render/TemporalPipelineState.h"

#include "StreamlineFrameGenerationContract.h"
#include "StreamlineFidelityFXContract.h"

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
		const ProviderResult transport{
			.code = ProviderResultCode::kFailure,
			.hresult = DXGI_ERROR_DEVICE_REMOVED,
			.message = "Presentation transport failed."
		};
		const auto transportMessage =
			FormatProviderFailure("Present", transport);
		Check(transportMessage.contains(std::to_string(
				  static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_REMOVED))),
			"failure reporting preserves the original HRESULT");
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
		SetPresentationActive(bool a_active) override
		{
			events.emplace_back(a_active ? "activate" : "deactivate");
			return presentationActivationSucceeds
				? Success()
				: Failure("presentation activation");
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
			return destroySucceeds ? Success() : Failure("destroy");
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
		bool destroySucceeds = true;
		bool presentationActivationSucceeds = true;
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
		Check(drainFailure.globalDrainAttempted &&
				  !drainFailure.globalDrainCompleted,
			"failed application-queue drain remains observable");
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

		provider = {};
		const auto releasePresentation = [&]() {
			provider.events.emplace_back("release-presentation");
		};
		const auto retirement =
			cs::render::temporal::RetirePresentationProvider(
				provider, [&]() {
					provider.events.emplace_back("drain");
					return true;
				}, releasePresentation);
		Check(retirement.Succeeded(),
			"presentation retirement completes after a proven queue drain");
		Check(provider.events ==
				  std::vector<std::string>{ "quiesce", "drain", "release",
					  "destroy", "release-presentation", "deactivate" },
			"swap-chain destruction reaches active provider hooks before "
			"unloading them");

		provider = {};
		provider.destroySucceeds = false;
		const auto destroyFailure =
			cs::render::temporal::RetirePresentationProvider(
				provider, [&]() {
					provider.events.emplace_back("drain");
					return true;
				}, releasePresentation);
		Check(!destroyFailure.Succeeded() &&
				  destroyFailure.globalDrainAttempted &&
				  destroyFailure.globalDrainCompleted &&
				  provider.events ==
					  std::vector<std::string>{ "quiesce", "drain",
						  "release", "destroy" },
			"failed destruction leaves presentation hooks active for "
			"quarantine");

		provider = {};
		provider.presentationActivationSucceeds = false;
		const auto deactivateFailure =
			cs::render::temporal::RetirePresentationProvider(
				provider, [&]() {
					provider.events.emplace_back("drain");
					return true;
				}, releasePresentation);
		Check(!deactivateFailure.Succeeded() &&
				  deactivateFailure.globalDrainAttempted &&
				  deactivateFailure.globalDrainCompleted &&
				  provider.events ==
					  std::vector<std::string>{ "quiesce", "drain",
						  "release", "destroy", "release-presentation", "deactivate" },
			"hook-disable failure is visible only after safe provider "
			"retirement");
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
		using namespace cs::render::temporal;
		PresentedFrameAccumulator generated;
		PresentedFrameAccumulator presented;
		sl::DLSSGStatus status = sl::DLSSGStatus::eOk;
		std::uint32_t calls = 0;
		FrameGenerationCapabilities observed{};
		for (const std::uint32_t count : { 0u, 1u, 4u }) {
			const auto result = cs::features::streamline_fg::PollState(
				sl::ViewportHandle{ 1 }, generated, presented, status,
				[&](sl::ViewportHandle, sl::DLSSGState& a_state,
					const sl::DLSSGOptions*) {
					++calls;
					a_state.numFramesActuallyPresented = count;
					a_state.numFramesToGenerateMax = 5;
					a_state.bIsDynamicMFGSupported =
						sl::Boolean::eTrue;
					a_state.status = sl::DLSSGStatus::eOk;
					return sl::Result::eOk;
				},
				[&](const sl::DLSSGState& a_state) {
					observed.configurationKnown = true;
					observed.maxGeneratedFrames =
						a_state.numFramesToGenerateMax;
					observed.dynamicModeSupported =
						a_state.bIsDynamicMFGSupported ==
						sl::Boolean::eTrue;
				});
			Check(result == sl::Result::eOk, "DLSS-G state query succeeds");
		}
		Check(calls == 3 && generated.Consume() == 5 &&
				presented.Consume() == 5 &&
				observed.maxGeneratedFrames == 5 &&
				observed.dynamicModeSupported,
			"the one DLSS-G state query retains counts and capabilities");

		FrameGenerationConfiguration configuration{};
		FrameGenerationCapabilities capabilities{
			.availability = CapabilityAvailability::kSupported,
			.configurationKnown = false,
			.maxGeneratedFrames = 1,
			.deviceGeneration = 4,
			.displayGeneration = 7,
			.sampledDeviceGeneration = 4,
			.sampledDisplayGeneration = 7
		};
		Check(cs::features::streamline_fg::ValidateConfiguration(
				  configuration, capabilities) ==
				  cs::features::streamline_fg::ConfigurationSupport::
					  kPendingCapabilities,
			"unknown current capabilities are not treated as unsupported");
		capabilities.configurationQueryFailed = true;
		Check(cs::features::streamline_fg::ValidateConfiguration(
				  configuration, capabilities) ==
				  cs::features::streamline_fg::ConfigurationSupport::
					  kUnsupported,
			"a failed runtime capability query is an explicit unavailable result");
		capabilities.configurationQueryFailed = false;
		capabilities.configurationKnown = true;
		Check(cs::features::streamline_fg::ValidateConfiguration(
				  configuration, capabilities) ==
				  cs::features::streamline_fg::ConfigurationSupport::
					  kSupported,
			"the ordinary fixed 2x request uses the reported one generated-frame capability");
		configuration.fixedMultiplier = 3;
		Check(cs::features::streamline_fg::ValidateConfiguration(
				  configuration, capabilities) ==
				  cs::features::streamline_fg::ConfigurationSupport::
					  kUnsupported,
			"a fixed multiplier above the reported maximum is rejected rather than clamped");
		configuration.mode = FrameGenerationMode::kDynamic;
		configuration.dynamicTargetFrameRate = 144.0f;
		Check(cs::features::streamline_fg::ValidateConfiguration(
				  configuration, capabilities) ==
				  cs::features::streamline_fg::ConfigurationSupport::
					  kUnsupported,
			"dynamic MFG remains unavailable until the runtime reports support");
		capabilities.dynamicModeSupported = true;
		Check(cs::features::streamline_fg::ValidateConfiguration(
				  configuration, capabilities) ==
				  cs::features::streamline_fg::ConfigurationSupport::
					  kSupported,
			"a finite positive dynamic target is accepted only with runtime support");

		configuration.mode = FrameGenerationMode::kFixed;
		configuration.fixedMultiplier = 4;
		auto configuredOptions =
			cs::features::streamline_fg::BuildOptions(
			true, configuration, 1280, 720, 2560, 1440, 3, true);
		Check(configuredOptions.mode == sl::DLSSGMode::eOn &&
				configuredOptions.numFramesToGenerate == 3 &&
				configuredOptions.colorWidth == 2560 &&
				configuredOptions.queueParallelismMode ==
					sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue &&
				configuredOptions.flags ==
					sl::DLSSGFlags::eRetainResourcesWhenOff,
			"fixed UI multiplier and presenting-queue ordering map to the "
			"documented SDK contract");
		configuration.mode = FrameGenerationMode::kDynamic;
		configuration.dynamicTargetFrameRate = 0.0f;
		configuredOptions =
			cs::features::streamline_fg::BuildOptions(
			true, configuration, 1280, 720, 2560, 1440, 3, true);
		Check(configuredOptions.mode == sl::DLSSGMode::eDynamic &&
				configuredOptions.dynamicTargetFrameRate == 0.0f,
			"dynamic zero target preserves the SDK automatic display target");

		sl::FSROptions fsrOptions{};
		sl::FSRAlgorithmOptions fsrAlgorithm{};
		Check(cs::features::streamline_fidelityfx::ClassifyCapability(
				  sl::Result::eErrorInvalidState,
				  sl::Boolean::eFalse) ==
				  CapabilityAvailability::kUnknown &&
				cs::features::streamline_fidelityfx::ClassifyCapability(
					sl::Result::eOk, sl::Boolean::eFalse) ==
					CapabilityAvailability::kUnsupported &&
				cs::features::streamline_fidelityfx::ClassifyCapability(
					sl::Result::eOk, sl::Boolean::eTrue) ==
					CapabilityAvailability::kSupported,
			"FSR 4 keeps a failed capability query distinct from an "
			"authoritative unsupported result");
		cs::features::streamline_fidelityfx::SelectAlgorithm(
			fsrOptions, fsrAlgorithm, sl::FSRAlgorithm::eFSR3);
		Check(fsrOptions.next == nullptr,
			"FSR 3 retains the established provider contract without an "
			"algorithm extension");
		cs::features::streamline_fidelityfx::SelectAlgorithm(
			fsrOptions, fsrAlgorithm, sl::FSRAlgorithm::eFSR4);
		Check(fsrOptions.next == &fsrAlgorithm &&
				fsrAlgorithm.algorithm == sl::FSRAlgorithm::eFSR4,
			"FSR 4 super resolution is selected by the explicit production "
			"algorithm chain");

		sl::FSRGOptions fsrgOptions{};
		sl::FSRGAlgorithmOptions fsrgAlgorithm{};
		cs::features::streamline_fidelityfx::SelectAlgorithm(
			fsrgOptions, fsrgAlgorithm, sl::FSRGAlgorithm::eFSR3);
		Check(fsrgOptions.next == nullptr,
			"FSR 3 frame generation retains the established provider contract");
		cs::features::streamline_fidelityfx::SelectAlgorithm(
			fsrgOptions, fsrgAlgorithm, sl::FSRGAlgorithm::eFSR4);
		Check(fsrgOptions.next == &fsrgAlgorithm &&
				fsrgAlgorithm.algorithm == sl::FSRGAlgorithm::eFSR4,
			"FSR 4 ML frame generation is selected by the explicit production "
			"algorithm chain");

		sl::FSRGState fsrgState{};
		fsrgState.completionMode = sl::FSRGCompletionMode::eFence;
		fsrgState.completionFence = reinterpret_cast<void*>(1);
		fsrgState.algorithm = sl::FSRGAlgorithm::eFSR4;
		fsrgState.available = sl::Boolean::eTrue;
		using FSRGValidation =
			cs::features::streamline_fidelityfx::FSRGStateValidation;
		Check(cs::features::streamline_fidelityfx::ValidateState(
				  fsrgState, sl::FSRGAlgorithm::eFSR4, false) ==
				  FSRGValidation::kValid,
			"MLFG preflight accepts the asynchronous fence capability before "
			"a Present submits a fence value");
		Check(cs::features::streamline_fidelityfx::ValidateState(
				  fsrgState, sl::FSRGAlgorithm::eFSR4, true) ==
				  FSRGValidation::kSubmittedDependencyMissing,
			"MLFG requires a real last-reader fence value after Present");
		fsrgState.completionFenceValue = 7;
		Check(cs::features::streamline_fidelityfx::ValidateState(
				  fsrgState, sl::FSRGAlgorithm::eFSR4, true) ==
				  FSRGValidation::kValid,
			"MLFG accepts the selected algorithm's asynchronous last-reader "
			"dependency");
		fsrgState.algorithm = sl::FSRGAlgorithm::eFSR3;
		Check(cs::features::streamline_fidelityfx::ValidateState(
				  fsrgState, sl::FSRGAlgorithm::eFSR4, true) ==
				  FSRGValidation::kAlgorithmUnavailable,
			"an FSR 3 state cannot masquerade as active FSR 4 MLFG");
		fsrgState.algorithm = sl::FSRGAlgorithm::eFSR4;
		fsrgState.completionMode =
			sl::FSRGCompletionMode::eVendorCompletionUnavailable;
		Check(cs::features::streamline_fidelityfx::ValidateState(
				  fsrgState, sl::FSRGAlgorithm::eFSR4, true) ==
				  FSRGValidation::kCompletionFenceUnavailable,
			"MLFG rejects a provider without the required asynchronous "
			"completion fence");

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
	TestDelayedRetirementAcrossRingCycles();
	TestPresentStatusOrchestration();
	TestStreamlineBackendContracts();
	TestProductionInputWriteOrdering();
	if (failures) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Frame-generation contract tests passed\n";
	return 0;
}
