#include "Render/FrameGenerationOrchestration.h"
#include "Render/FrameGenerationCpuTiming.h"
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

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
	int failures = 0;
	std::array<std::uint64_t, 8> fakeClockValues{};
	std::size_t fakeClockIndex = 0;

	std::uint64_t FakeClock() noexcept
	{
		return fakeClockValues[fakeClockIndex++];
	}

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
		const ProviderResult failure{
			.code = ProviderResultCode::kFailure,
			.sdkResult = -12,
			.message = "Frame interpolation failed."
		};
		const auto message = FormatProviderFailure("Collect present status", failure);
		Check(
			message == "Collect present status: Frame interpolation failed. (SDK result -12)",
			"failure reason preserves its operation and exact signed SDK code");
		TopologyState topology;
		RequestedTopology requested;
		requested.frameGenerationEligible = true;
		requested.frameGeneration = FrameGenerationMethod::kFSR3;
		Check(topology.Freeze(requested), "failure reporting topology freezes");
		SessionTopology session;
		session.valid = true;
		session.proxyInstalled = true;
		session.admittedFg = FrameGenerationMethod::kFSR3;
		Check(topology.Admit(session), "failure reporting topology admits");
		topology.Quarantine(
			ClassifyFailure(FailureDomain::kFrameGeneration,
				SuperResolutionMethod::kNone, FrameGenerationMethod::kFSR3),
			1, message);
		Check(
			!topology.Effective().frameGenerationEnabled &&
				topology.Effective().frameGeneration == FrameGenerationMethod::kOff &&
				topology.Request()->frameGeneration == FrameGenerationMethod::kFSR3 &&
				topology.Pending().required && topology.Pending().reason.contains(message),
			"provider rejection disables effective FG while retaining the request and exact failure reason");
	}

	void TestCpuPhaseTimingCollector()
	{
		using Phase = cs::render::FrameGenerationCpuPhase;
		cs::render::FrameGenerationCpuTimingCollector<3> collector(&FakeClock);

		fakeClockIndex = 0;
		{
			auto scope = collector.Measure(Phase::kLatencySleep);
		}
		Check(
			fakeClockIndex == 0 && !collector.GetSnapshot().available,
			"disabled CPU phase timing does not invoke the clock");

		collector.SetEnabled(true);
		collector.RecordNanoseconds(Phase::kLatencySleep, 1'000'000);
		collector.RecordNanoseconds(Phase::kLatencySleep, 2'000'000);
		collector.RecordNanoseconds(Phase::kLatencySleep, 3'000'000);
		collector.RecordNanoseconds(Phase::kLatencySleep, 4'000'000);
		collector.RecordNanoseconds(Phase::kSdkPresent, 8'000'000);
		collector.RecordFrameTimeInput(15.5);
		auto snapshot = collector.GetSnapshot();
		const auto& sleep = snapshot.phases[
			static_cast<std::size_t>(Phase::kLatencySleep)];
		const auto& present = snapshot.phases[
			static_cast<std::size_t>(Phase::kSdkPresent)];
		Check(
			snapshot.available && sleep.sampleCount == 4 &&
				sleep.windowSampleCount == 3 &&
				sleep.windowMeanMilliseconds == 3.0 &&
				sleep.windowMaxMilliseconds == 4.0,
			"CPU phase timing keeps a deterministic bounded rolling window");
		Check(
			present.sampleCount == 1 &&
				present.windowMeanMilliseconds == 8.0 &&
				snapshot.frameTimeInputAvailable &&
				snapshot.lastFrameTimeInputMilliseconds == 15.5 &&
				snapshot.phases[
					static_cast<std::size_t>(Phase::kPrepareFrame)]
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
		const auto& prepare = snapshot.phases[
			static_cast<std::size_t>(Phase::kPrepareFrame)];
		Check(
			fakeClockIndex == 2 && prepare.sampleCount == 1 &&
				prepare.windowMeanMilliseconds == 2.0,
			"CPU phase timing records scopes that leave through an early return");

		fakeClockValues = { 20, 1'000'020 };
		fakeClockIndex = 0;
		try {
			auto scope = collector.Measure(Phase::kPrepareFrame);
			throw 1;
		} catch (...) {
		}
		snapshot = collector.GetSnapshot();
		Check(
			fakeClockIndex == 2 &&
				snapshot.phases[
					static_cast<std::size_t>(Phase::kPrepareFrame)]
						.sampleCount == 2,
			"CPU phase timing records scopes that leave through an exception");

		collector.SetEnabled(false);
		fakeClockIndex = 0;
		{
			auto scope = collector.Measure(Phase::kSdkPresent);
		}
		snapshot = collector.GetSnapshot();
		Check(
			fakeClockIndex == 0 && !snapshot.available &&
				!snapshot.frameTimeInputAvailable &&
				snapshot.phases[
					static_cast<std::size_t>(Phase::kSdkPresent)]
						.sampleCount == 0,
			"disabling CPU phase timing clears samples and reports unavailable");
	}

	std::string ReadTextFile(const char* a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}

	void TestProductionCpuTimingHooks(
		const char* a_pipelinePath,
		const char* a_swapChainPath)
	{
		const auto pipeline = ReadTextFile(a_pipelinePath);
		const auto swapChain = ReadTextFile(a_swapChainPath);
		const auto checkNear =
			[](const std::string& a_source,
				std::string_view a_timing,
				std::string_view a_call,
				std::size_t a_distance,
				std::string_view a_message) {
				const auto timing = a_source.find(a_timing);
				const auto call = a_source.find(a_call, timing);
				Check(
					timing != std::string::npos &&
						call != std::string::npos &&
						call - timing <= a_distance,
					a_message);
			};
		checkNear(
			pipeline,
			"FrameGenerationCpuPhase::kLatencySleep",
			"_impl->activePresentation->Sleep(",
			300,
			"latency Sleep timing wraps the selected provider call");
		for (const auto& [timing, call, distance] :
			std::array{
				std::tuple{ "kAcquirePresentInputs",
					"_provider->AcquirePresentInputs()", 300u },
				std::tuple{ "kAllocatorFenceWait",
					"WaitForFrame(_frameSlot)", 300u },
				std::tuple{ "kCopyRecord",
					"commandList->CopyResource(", 1'200u },
				std::tuple{ "kPrepareFrame",
					"PrepareFrameSafely(", 300u },
				std::tuple{ "kSdkPresent",
					"_swapChain->Present(a_syncInterval, a_flags)", 300u },
				std::tuple{ "kCollectPresentStatus",
					"CollectAcceptedPresentStatus(", 300u } }) {
			checkNear(
				swapChain,
				timing,
				call,
				distance,
				std::string("production timing wraps its intended call: ") +
					timing);
		}
		Check(
			swapChain.find("if (a_flags & DXGI_PRESENT_TEST)") <
				swapChain.find("FrameGenerationCpuPhase::kSdkPresent"),
			"SDK Present timing remains after the TEST-present early return");
	}

	class RecordingProvider final :
		public cs::render::temporal::IFrameGenerationProvider
	{
	public:
		const char* Name() const noexcept override { return "recording"; }
		cs::render::temporal::ProviderResult PrepareDevice(ID3D12Device**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult PrepareFactory(IDXGIFactory4**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult CreatePresentation(
			const cs::render::temporal::PresentationCreateContext&,
			IDXGISwapChain4**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult CreateDisplayResources(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT,
			std::uint32_t) override
		{
			events.emplace_back(
				"create:" + std::to_string(a_width) + "x" +
				std::to_string(a_height));
			return createSucceeds ? Success() : Failure("create");
		}
		cs::render::temporal::ProviderResult PrepareFrame(
			const cs::render::temporal::FrameGenerationRequest&) override
		{
			events.emplace_back("prepare");
			return prepareSucceeds ? Success() : Failure("prepare");
		}
		cs::render::temporal::ProviderResult CancelFrame(
			const cs::render::temporal::FrameGenerationRequest&) override
		{
			events.emplace_back("cancel");
			return cancelSucceeds ? Success() : Failure("cancel");
		}
		cs::render::temporal::ProviderResult SetGenerationEnabled(bool a_enabled) override
		{
			events.emplace_back(a_enabled ? "enable" : "disable");
			return Success();
		}
		cs::render::temporal::ProviderResult AcquirePresentInputs() override
		{
			events.emplace_back("acquire");
			return acquireSucceeds ? Success() : Failure("acquire");
		}
		cs::render::temporal::ProviderResult CollectPresentStatus(
			UINT a_flags,
			HRESULT a_result) override
		{
			++statusCalls;
			lastStatusFlags = a_flags;
			lastPresentResult = a_result;
			return Success();
		}
		std::optional<std::uint32_t>
		ConsumeGeneratedFrameCount() noexcept override
		{
			return generatedCount;
		}
		std::optional<std::uint32_t>
		ConsumePresentedFrameCount() noexcept override
		{
			return presentedCount;
		}
		cs::render::temporal::ProviderResult Sleep(std::uint32_t) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult SetLatencyMarker(
			cs::render::temporal::LatencyMarker,
			std::uint32_t) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult Quiesce() override
		{
			events.emplace_back("quiesce");
			return quiesceSucceeds ? Success() : Failure("quiesce");
		}
		cs::render::temporal::ProviderResult ReleaseDisplayResources() noexcept override
		{
			events.emplace_back("release");
			return releaseSucceeds ? Success() : Failure("release");
		}
		cs::render::temporal::ProviderResult DestroyAfterDrain() noexcept override
		{
			events.emplace_back("destroy");
			return Success();
		}
		bool IsReady() const noexcept override { return true; }

		static cs::render::temporal::ProviderResult Success()
		{
			return {
				.code =
					cs::render::temporal::ProviderResultCode::kSuccess
			};
		}
		static cs::render::temporal::ProviderResult Failure(std::string a_message)
		{
			return {
				.code =
					cs::render::temporal::ProviderResultCode::kFailure,
				.message = std::move(a_message)
			};
		}

		std::vector<std::string> events;
		bool prepareSucceeds = true;
		bool cancelSucceeds = true;
		bool acquireSucceeds = true;
		bool quiesceSucceeds = true;
		bool releaseSucceeds = true;
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
		const auto result = cs::render::temporal::QuiesceDrainAndRelease(
			provider,
			[&]() {
				provider.events.emplace_back("drain");
				return true;
			});
		Check(result.Succeeded(), "successful lifecycle completes");
		Check(
			provider.events ==
				std::vector<std::string>{ "quiesce", "drain", "release" },
			"provider disable/quiesce precedes CPU drain and release");

		provider.events.clear();
		const auto drainFailure =
			cs::render::temporal::QuiesceDrainAndRelease(
				provider,
				[&]() {
					provider.events.emplace_back("drain");
					return false;
				});
		Check(!drainFailure.Succeeded(), "drain failure is visible");
		Check(
			provider.events ==
				std::vector<std::string>{ "quiesce", "drain" },
			"drain failure never frees provider resources");

		provider.events.clear();
		provider.quiesceSucceeds = false;
		const auto quiesceFailure =
			cs::render::temporal::QuiesceDrainAndRelease(
				provider,
				[&]() {
					provider.events.emplace_back("drain");
					return true;
				});
		Check(!quiesceFailure.Succeeded(), "quiesce failure is visible");
		Check(
			provider.events == std::vector<std::string>{ "quiesce" },
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
		Check(
			provider.events.back() == "create:1280x720",
			"same-size resize recreates provider display resources");

		result = cs::render::temporal::RestoreProviderAfterResize(
			provider, S_OK, oldDescription, changedDescription);
		Check(result.Succeeded(), "changed-size resize restores provider readiness");
		Check(
			provider.events.back() == "create:1920x1080",
			"changed-size resize uses the actual new description");

		const HRESULT rejected = DXGI_ERROR_INVALID_CALL;
		const auto restoration =
			cs::render::temporal::RestoreProviderAndPreserveResizeResult(
			provider, rejected, oldDescription, std::nullopt);
		Check(
			restoration.providerResult.Succeeded(),
			"rejected resize attempts old-provider restoration");
		Check(
			provider.events.back() == "create:1280x720",
			"rejected resize restores from the retained description");
		Check(
			restoration.nativeResizeResult == rejected,
			"provider restoration preserves the rejected native resize HRESULT");

		provider.createSucceeds = false;
		result = cs::render::temporal::RestoreProviderAfterResize(
			provider, S_OK, oldDescription, oldDescription);
		Check(!result.Succeeded(), "provider recreation failure remains visible for quarantine");
		Check(
			cs::render::temporal::CompleteNativeResize(
				S_OK, S_FALSE) == S_OK,
			"provider-only recreation failure preserves a successful native resize");
		Check(
			cs::render::temporal::CompleteNativeResize(
				S_OK, E_OUTOFMEMORY) == E_OUTOFMEMORY,
			"bridge-resource failure publishes an explicit invalid resize result");
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
		gate.MarkSubmitted(1, true);
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
			resized,
			proxy,
			inner,
			fenceValues,
			gate,
			frameSlot,
			presentPrepared,
			preparedGeneration,
			vendorConsumption,
			preparedTransaction);
		Check(
			proxy.BufferDesc.Width == 1920 &&
				proxy.BufferDesc.Height == 1080 &&
				inner.Width == 1920 &&
				inner.Height == 1080,
			"successful native resize commits the actual bridge descriptor");
		Check(
			fenceValues == std::array<std::uint64_t, 2>{ 0, 0 } &&
				frameSlot == 0 && !gate.IsPending(1) &&
				!presentPrepared && !preparedGeneration &&
				!vendorConsumption && !preparedTransaction,
			"resize commit invalidates prepared and retry protocol state");
		Check(
			cs::render::temporal::CompleteNativeResize(
				S_OK, S_FALSE) == S_OK &&
				proxy.BufferDesc.Width == 1920 &&
				inner.Width == 1920,
			"provider quarantine after commit cannot roll published metadata back to the old native size");
	}

	void TestSafePreparation()
	{
		RecordingProvider provider;
		cs::render::temporal::FrameGenerationRequest request;
		provider.prepareSucceeds = false;
		const auto safe =
			cs::render::temporal::PrepareFrameSafely(provider, request);
		Check(!safe.prepared && safe.safeToPresent, "failed preparation is safe only after cancellation");
		provider.events.emplace_back("present");
		Check(
			provider.events ==
				std::vector<std::string>{ "prepare", "cancel", "present" },
			"SDK disable/cancellation precedes the same underlying Present");

		provider.events.clear();
		provider.cancelSucceeds = false;
		const auto unsafe =
			cs::render::temporal::PrepareFrameSafely(provider, request);
		if (unsafe.safeToPresent) {
			provider.events.emplace_back("present");
		}
		Check(!unsafe.safeToPresent, "failed cancellation blocks unsafe interpolation submission");
		Check(
			provider.events ==
				std::vector<std::string>{ "prepare", "cancel" },
			"unsafe cancellation path does not call Present");
	}

	void TestInputReuseGate()
	{
		cs::render::temporal::PresentInputReuseGate gate;
		gate.MarkSubmitted(0, true);
		std::uint32_t acquisitions = 0;
		bool complete = false;
		Check(
			!gate.Acquire(0, [&]() {
				++acquisitions;
				return complete;
			}),
			"delayed provider completion blocks borrowed-input reuse");
		Check(gate.IsPending(0), "failed acquisition keeps the slot pending");

		complete = true;
		Check(
			gate.Acquire(0, [&]() {
				++acquisitions;
				return complete;
			}),
			"completed provider use permits the first shared write");
		Check(
			gate.Acquire(0, [&]() {
				++acquisitions;
				return complete;
			}),
			"alpha, raw, and HUD-less writes share one acquisition");
		Check(acquisitions == 2, "successful acquisition runs once for the producer frame");
	}

	void TestStatusAndAccountingPolicies()
	{
		cs::render::temporal::PresentedFrameAccumulator counts;
		counts.Add(0);
		counts.Add(1);
		counts.Add(4);
		Check(
			counts.Consume() == 5,
			"read-reset SDK counts retain zero, one, and multiple presented frames");
		Check(counts.Consume() == 0, "presented-frame accumulator is read-reset");

		Check(
			static_cast<std::uint32_t>(
				ffx::ReturnCode::ErrorProviderNoSupportNewDesctype) == 7,
			"FidelityFX return code 7 remains named and numerically intact");
		ffx::QueryGetProviderVersion version;
		Check(
			version.header.type ==
				FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION,
			"FidelityFX provider-version query uses the compiled descriptor type");
	}

	void TestPresentStatusOrchestration()
	{
		Check(
			cs::render::temporal::ShouldObservePresentStatus(0, S_OK),
			"accepted real Present observes provider status");
		Check(
			!cs::render::temporal::ShouldObservePresentStatus(
				DXGI_PRESENT_TEST, S_OK),
			"TEST Present never observes provider status");
		Check(
			!cs::render::temporal::ShouldObservePresentStatus(
				0, DXGI_ERROR_WAS_STILL_DRAWING),
			"retryable Present does not replay the previous status snapshot");
		Check(
			!cs::render::temporal::ShouldObservePresentStatus(
				0, DXGI_ERROR_DEVICE_REMOVED),
			"failed native Present does not publish stale provider status");

		RecordingProvider provider;
		provider.generatedCount = 2;
		provider.presentedCount = 3;
		auto collected =
			cs::render::temporal::CollectAcceptedPresentStatus(
				provider, 0, S_OK);
		Check(
			collected.observed && provider.statusCalls == 1 &&
				collected.generatedFrames == 2u &&
				collected.presentedFrames == 3u &&
				provider.lastStatusFlags == 0 &&
				provider.lastPresentResult == S_OK,
			"production status orchestration polls and retains counts after one accepted real Present");
		collected =
			cs::render::temporal::CollectAcceptedPresentStatus(
				provider, DXGI_PRESENT_TEST, S_OK);
		Check(
			!collected.observed && provider.statusCalls == 1,
			"production status orchestration does not poll TEST Presents");
		collected =
			cs::render::temporal::CollectAcceptedPresentStatus(
				provider, 0, DXGI_ERROR_WAS_STILL_DRAWING);
		Check(
			!collected.observed && provider.statusCalls == 1,
			"production status orchestration does not poll retry attempts");

	}

	void TestStreamlineBackendContracts()
	{
		cs::render::temporal::PresentedFrameAccumulator counts;
		sl::DLSSGStatus status = sl::DLSSGStatus::eOk;
		std::uint32_t calls = 0;
		for (const std::uint32_t count : { 0u, 1u, 4u }) {
			const auto result = cs::features::streamline_fg::PollState(
				sl::ViewportHandle{ 1 },
				counts,
				status,
				[&](sl::ViewportHandle,
					sl::DLSSGState& a_state,
					const sl::DLSSGOptions*) {
					++calls;
					a_state.numFramesActuallyPresented = count;
					a_state.status = sl::DLSSGStatus::eOk;
					return sl::Result::eOk;
				});
			Check(result == sl::Result::eOk, "DLSS-G state query succeeds");
		}
		Check(
			calls == 3 && counts.Consume() == 5,
			"the one DLSS-G state owner retains 0, 1, and multiple presentations");

		std::vector<std::string> cleanupEvents;
		sl::DLSSGOptions recordedOptions{};
		const auto cleanup = cs::features::streamline_fg::DestroyResources(
			true,
			sl::ViewportHandle{ 9 },
			[&]() {
				cleanupEvents.emplace_back("clear");
				return sl::Result::eOk;
			},
			[&](sl::ViewportHandle,
				const sl::DLSSGOptions& a_options) {
				cleanupEvents.emplace_back("options");
				recordedOptions = a_options;
				return sl::Result::eOk;
			},
			[&](sl::Feature a_feature, sl::ViewportHandle) {
				cleanupEvents.emplace_back("free");
				Check(
					a_feature == sl::kFeatureDLSS_G,
					"DLSS-G cleanup frees only the DLSS-G allocation");
				return sl::Result::eOk;
			});
		Check(
			cleanup.succeeded &&
				cleanupEvents == std::vector<std::string>{
					"clear", "options", "free" } &&
				recordedOptions.mode == sl::DLSSGMode::eOff,
			"DLSS-G cleanup checks clear, disable, and free in order");

		cleanupEvents.clear();
		const auto idempotent =
			cs::features::streamline_fg::DestroyResources(
				false,
				sl::ViewportHandle{ 9 },
				[&]() {
					cleanupEvents.emplace_back("clear");
					return sl::Result::eOk;
				},
				[&](sl::ViewportHandle,
					const sl::DLSSGOptions&) {
					cleanupEvents.emplace_back("options");
					return sl::Result::eOk;
				},
				[&](sl::Feature, sl::ViewportHandle) {
					cleanupEvents.emplace_back("free");
					return sl::Result::eOk;
				});
		Check(
			idempotent.succeeded && cleanupEvents.empty(),
			"unallocated DLSS-G cleanup is idempotent and makes no SDK calls");

		cleanupEvents.clear();
		const auto clearFailure =
			cs::features::streamline_fg::DestroyResources(
				true,
				sl::ViewportHandle{ 9 },
				[&]() {
					cleanupEvents.emplace_back("clear");
					return sl::Result::eErrorInvalidState;
				},
				[&](sl::ViewportHandle,
					const sl::DLSSGOptions&) {
					cleanupEvents.emplace_back("options");
					return sl::Result::eOk;
				},
				[&](sl::Feature, sl::ViewportHandle) {
					cleanupEvents.emplace_back("free");
					return sl::Result::eOk;
				});
		Check(
			!clearFailure.succeeded &&
				cleanupEvents == std::vector<std::string>{ "clear" },
			"DLSS-G cleanup failure is propagated before later release calls");

		cleanupEvents.clear();
		const auto optionsFailure =
			cs::features::streamline_fg::DestroyResources(
				true,
				sl::ViewportHandle{ 9 },
				[&]() {
					cleanupEvents.emplace_back("clear");
					return sl::Result::eOk;
				},
				[&](sl::ViewportHandle,
					const sl::DLSSGOptions&) {
					cleanupEvents.emplace_back("options");
					return sl::Result::eErrorInvalidState;
				},
				[&](sl::Feature, sl::ViewportHandle) {
					cleanupEvents.emplace_back("free");
					return sl::Result::eOk;
				});
		Check(
			!optionsFailure.succeeded &&
				cleanupEvents == std::vector<std::string>{
					"clear", "options" },
			"DLSS-G disable failure prevents resource free");

		cleanupEvents.clear();
		const auto freeFailure =
			cs::features::streamline_fg::DestroyResources(
				true,
				sl::ViewportHandle{ 9 },
				[&]() {
					cleanupEvents.emplace_back("clear");
					return sl::Result::eOk;
				},
				[&](sl::ViewportHandle,
					const sl::DLSSGOptions&) {
					cleanupEvents.emplace_back("options");
					return sl::Result::eOk;
				},
				[&](sl::Feature, sl::ViewportHandle) {
					cleanupEvents.emplace_back("free");
					return sl::Result::eErrorInvalidState;
				});
		Check(
			!freeFailure.succeeded &&
				cleanupEvents == std::vector<std::string>{
					"clear", "options", "free" },
			"DLSS-G free failure remains visible and does not report safe cleanup");
	}

	void TestFidelityFXBackendContracts()
	{
		auto* swapChain = reinterpret_cast<IDXGISwapChain4*>(0x1234);
		const auto config =
			cs::features::fidelityfx_fg::BuildDisabledConfiguration(
				swapChain, 88, 1920, 1080);
		Check(
			!config.frameGenerationEnabled &&
				config.frameGenerationCallback == nullptr &&
				config.frameGenerationCallbackUserContext == nullptr &&
				config.HUDLessColor.resource == nullptr &&
				config.presentCallback == nullptr &&
				config.presentCallbackUserContext == nullptr &&
				config.swapChain == swapChain &&
				config.frameID == 88 &&
				config.flags == 0 &&
				config.generationRect.width == 1920 &&
				config.generationRect.height == 1080,
			"FSR deconfiguration detaches callbacks, user context, and HUD-less input before destruction");

		ffx::Context context{};
		const auto unsupported =
			cs::features::fidelityfx_fg::QueryProviderVersion(
				context,
				[](ffx::Context&,
					ffx::QueryGetProviderVersion&) {
					return ffx::ReturnCode::
						ErrorProviderNoSupportNewDesctype;
				});
		Check(
			!unsupported.available && unsupported.id == 0 &&
				unsupported.name.empty() &&
				unsupported.result ==
					ffx::ReturnCode::
						ErrorProviderNoSupportNewDesctype,
			"unsupported FSR provider-version query is explicit and non-fatal");
	}

	void TestProductionInputWriteOrdering()
	{
		cs::render::temporal::PresentInputReuseGate gate;
		gate.MarkSubmitted(1, true);
		bool providerComplete = false;
		std::vector<std::string> events;
		auto write = [&](std::string a_name) {
			return cs::render::temporal::WriteFrameGenerationInput(
				[&]() {
					return gate.Acquire(1, [&]() {
						events.emplace_back("provider-wait");
						return providerComplete;
					});
				},
				[&]() {
					events.emplace_back(std::move(a_name));
				});
		};
		Check(
			!write("alpha") &&
				events == std::vector<std::string>{ "provider-wait" },
			"delayed completion prevents the first shared write");
		providerComplete = true;
		Check(write("alpha"), "completed slot permits alpha input write");
		Check(write("raw"), "same slot permits raw input write");
		Check(write("hudless"), "same slot permits HUD-less input write");
		Check(
			events == std::vector<std::string>{
				"provider-wait", "provider-wait", "alpha", "raw",
				"hudless" },
			"production write wrapper waits before the first write and only once after completion across alpha, raw, and HUD-less inputs");
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
		Check(
			result ==
				cs::render::temporal::DestructionResult::kSuccess,
			"callback deconfiguration lifecycle succeeds");
		Check(
			events == std::vector<std::string>{
				"disable-null-callbacks", "drain", "destroy-context" },
			"callbacks are detached before drain and context destruction");

		events.clear();
		const auto disableFailure =
			cs::render::temporal::DisableDrainAndDestroy(
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
		Check(
			disableFailure ==
				cs::render::temporal::DestructionResult::kDisableFailed &&
				events ==
					std::vector<std::string>{ "disable-null-callbacks" },
			"failed callback deconfiguration prevents drain and destruction");
	}
}

int main(int a_argc, char** a_argv)
{
	TestCpuPhaseTimingCollector();
	TestFailureReporting();
	TestLifecycleOrderAndFailures();
	TestResizeRestoration();
	TestResizeCommitProtocol();
	TestSafePreparation();
	TestInputReuseGate();
	TestStatusAndAccountingPolicies();
	TestPresentStatusOrchestration();
	TestStreamlineBackendContracts();
	TestFidelityFXBackendContracts();
	TestProductionInputWriteOrdering();
	TestDisableDrainDestroyOrder();
	Check(a_argc == 3, "production source paths are supplied");
	if (a_argc == 3) {
		TestProductionCpuTimingHooks(a_argv[1], a_argv[2]);
	}
	if (failures) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Frame-generation contract tests passed\n";
	return 0;
}
