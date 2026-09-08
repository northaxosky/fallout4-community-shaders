#include "Render/TemporalPipelineState.h"
#include "Render/TemporalProvider.h"
#include "Render/TemporalRenderSizing.h"
#include "Render/TemporalDevicePolicy.h"
#include "SuperResolutionContext.h"

#include <iostream>
#include <string_view>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	void TestRequestedEffectiveAndPending()
	{
		using namespace cs::render::temporal;

		const auto fgFailure = ClassifyFailure(
			FailureDomain::kFrameGeneration, SuperResolutionMethod::kDLSS, FrameGenerationMethod::kFSR3);
		Check(!fgFailure.superResolution && fgFailure.frameGeneration,
			"FG algorithm failure does not quarantine independent SR");
		const auto sharedFailure = ClassifyFailure(
			FailureDomain::kStreamline, SuperResolutionMethod::kDLSS, FrameGenerationMethod::kDLSSG);
		Check(sharedFailure.superResolution && sharedFailure.frameGeneration,
			"Streamline failure quarantines both dependent consumers");
		const auto unrelatedFailure = ClassifyFailure(
			FailureDomain::kStreamline, SuperResolutionMethod::kFSR3, FrameGenerationMethod::kXeSS);
		Check(!unrelatedFailure.superResolution && !unrelatedFailure.frameGeneration,
			"Streamline failure preserves unrelated vendor consumers");
		const auto transportFailure = ClassifyFailure(
			FailureDomain::kTransport, SuperResolutionMethod::kFSR3, FrameGenerationMethod::kXeSS);
		Check(transportFailure.superResolution && transportFailure.frameGeneration,
			"shared transport failure stops both consumers");

		TopologyState state;
		RequestedTopology requested;
		requested.upscalingEligible = true;
		requested.frameGenerationEligible = true;
		requested.superResolution = SuperResolutionMethod::kDLSS;
		requested.noDlssFallback = SuperResolutionMethod::kFSR3;
		requested.frameGeneration = FrameGenerationMethod::kFSR3;
		requested.revision = 4;
		Check(state.Freeze(requested), "first request freezes");
		Check(!state.Freeze(requested), "request freezes exactly once");

		SessionTopology session;
		session.valid = true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kNone)] = true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kTAA)] = true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kFSR3)] = true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kDLSS)] = true;
		session.admittedFg = FrameGenerationMethod::kFSR3;
		Check(state.Admit(session), "session admits after request");
		Check(state.Effective().superResolution == SuperResolutionMethod::kDLSS, "requested DLSS is effective");
		Check(state.Effective().frameGeneration == FrameGenerationMethod::kFSR3, "requested FSR FG is effective");

		state.SubmitLive(
			true,
			SuperResolutionMethod::kFSR3,
			2,
			false,
			FrameGenerationMethod::kFSR3,
			5);
		Check(state.Effective().superResolution == SuperResolutionMethod::kFSR3, "admitted SR switches live");
		Check(!state.Effective().frameGenerationEnabled, "FG enablement switches live");
		Check(!state.Pending().required, "admitted live changes do not require restart");

		state.SubmitLive(
			true,
			SuperResolutionMethod::kXeSS,
			2,
			true,
			FrameGenerationMethod::kDLSSG,
			6);
		Check(state.Effective().superResolution == SuperResolutionMethod::kFSR3, "unadmitted SR does not replace live provider");
		Check(state.Effective().frameGeneration == FrameGenerationMethod::kFSR3, "unadmitted FG does not replace live provider");
		Check(state.Pending().required, "unadmitted topology is pending restart");

		state.FailSuperResolutionToNative(
			7, "external provider failed after render commitment");
		Check(
			state.Effective().superResolution == SuperResolutionMethod::kTAA,
			"runtime SR failure switches to native TAA");
		Check(
			state.Effective().revision == 7,
			"runtime SR failure advances the effective revision");
		Check(
			state.Pending().required,
			"runtime SR failure requires restart before external SR can resume");
		state.SubmitLive(
			true,
			SuperResolutionMethod::kDLSS,
			1,
			false,
			FrameGenerationMethod::kFSR3,
			8);
		Check(
			state.Effective().superResolution == SuperResolutionMethod::kTAA,
			"live configuration cannot re-enable a failed external SR session");
		Check(
			state.Request()->superResolution == SuperResolutionMethod::kDLSS,
			"requested topology reports the failed live selection");
		state.SubmitLive(
			true,
			SuperResolutionMethod::kNone,
			1,
			false,
			FrameGenerationMethod::kFSR3,
			9);
		Check(
			state.Effective().superResolution == SuperResolutionMethod::kNone,
			"native policy remains selectable after an external SR failure");
		Check(
			!state.Pending().required,
			"selecting a native policy clears the external-provider restart request");
		state.SubmitLive(
			true,
			SuperResolutionMethod::kFSR3,
			1,
			false,
			FrameGenerationMethod::kFSR3,
			10);
		Check(
			state.Request()->superResolution == SuperResolutionMethod::kFSR3 &&
				state.Effective().superResolution == SuperResolutionMethod::kTAA,
			"requested and effective topology remain distinct after failure");
	}

	void TestQuarantinedConfiguration()
	{
		using namespace cs::render::temporal;
		for (const auto domain : {
				 FailureDomain::kSuperResolution,
				 FailureDomain::kFrameGeneration,
				 FailureDomain::kEngine,
				 FailureDomain::kTransport,
				 FailureDomain::kStreamline }) {
			TopologyState state;
			RequestedTopology requested;
			requested.upscalingEligible = true;
			requested.frameGenerationEligible = true;
			requested.superResolution = SuperResolutionMethod::kDLSS;
			requested.frameGeneration = FrameGenerationMethod::kXeSS;
			Check(state.Freeze(requested), "failure fixture freezes");
			SessionTopology session;
			session.valid = true;
			session.admittedSr.fill(true);
			session.admittedFg = FrameGenerationMethod::kXeSS;
			Check(state.Admit(session), "failure fixture admits");

			const auto impact = ClassifyFailure(
				domain, requested.superResolution, requested.frameGeneration);
			state.Quarantine(impact, 11, "provider preflight failed");
			Check(
				state.Request()->superResolution == requested.superResolution &&
					state.Request()->frameGeneration == requested.frameGeneration,
				"quarantine preserves the user's requested providers");
			Check(
				state.Effective().superResolutionEnabled == !impact.superResolution &&
					state.Effective().frameGenerationEnabled == !impact.frameGeneration,
				"effective enablement reflects the quarantined consumers");
			Check(
				state.Effective().superResolution == (impact.superResolution
					? SuperResolutionMethod::kNone : requested.superResolution) &&
					state.Effective().frameGeneration == (impact.frameGeneration
						? FrameGenerationMethod::kOff : requested.frameGeneration),
				"quarantined providers are not reported as effective");
			Check(state.Effective().revision == 11, "quarantine advances the revision");
			Check(
				state.Pending().required &&
					state.Pending().reason.contains("provider preflight failed"),
				"quarantine exposes its actual cause and restart requirement");

			state.SubmitLive(
				true, requested.superResolution, 3,
				true, requested.frameGeneration, 12);
			Check(
				state.Effective().superResolutionEnabled == !impact.superResolution &&
					state.Effective().frameGenerationEnabled == !impact.frameGeneration,
				"live settings cannot reactivate quarantined consumers");
			Check(
				state.Pending().required &&
					state.Pending().reason.contains("provider preflight failed"),
				"changing quality does not erase the failure's restart notice");

			if (impact.superResolution) {
				state.FailSuperResolutionToNative(13, "queued fallback");
				Check(
					!state.Effective().superResolutionEnabled &&
						state.Effective().superResolution == SuperResolutionMethod::kNone,
					"a queued resolve fallback cannot re-enable a quarantined renderer");
			}
		}
	}

	void TestTemporalFeatureLevels()
	{
		using namespace cs::render::temporal;
		RequestedTopology request;
		request.upscalingEligible = true;
		request.superResolution = SuperResolutionMethod::kXeSS;
		request.frameGenerationEligible = true;
		request.frameGeneration = FrameGenerationMethod::kXeSS;
		std::vector<D3D_FEATURE_LEVEL> levels{ D3D_FEATURE_LEVEL_11_0 };
		ConfigureTemporalFeatureLevels(request, levels);
		Check(
			levels == std::vector<D3D_FEATURE_LEVEL>{
				D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 },
			"XeSS startup requests the feature level needed for live FSR selection");
		const auto once = levels;
		ConfigureTemporalFeatureLevels(request, levels);
		Check(levels == once, "feature-level preference is idempotent");

		levels = {
			D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_1
		};
		ConfigureTemporalFeatureLevels(request, levels);
		Check(
			levels == std::vector<D3D_FEATURE_LEVEL>{
				D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0 },
			"higher feature levels retain priority and lower levels remain fallbacks");

		levels.clear();
		ConfigureTemporalFeatureLevels(request, levels);
		Check(
			levels.size() == 7 && levels.front() == D3D_FEATURE_LEVEL_11_1 &&
				levels[1] == D3D_FEATURE_LEVEL_11_0 &&
				levels.back() == D3D_FEATURE_LEVEL_9_1,
			"an empty engine list preserves D3D11's default fallback levels");

		request.upscalingEligible = false;
		levels = { D3D_FEATURE_LEVEL_11_0 };
		ConfigureTemporalFeatureLevels(request, levels);
		Check(
			levels == std::vector<D3D_FEATURE_LEVEL>{ D3D_FEATURE_LEVEL_11_0 },
			"XeSS FG alone does not require an unused FSR device upgrade");
		request.frameGeneration = FrameGenerationMethod::kDLSSG;
		ConfigureTemporalFeatureLevels(request, levels);
		Check(
			levels.front() == D3D_FEATURE_LEVEL_11_1,
			"DLSS-G preserves its existing D3D11 feature-level preference");

		request.frameGenerationEligible = false;
		levels.clear();
		ConfigureTemporalFeatureLevels(request, levels);
		Check(levels.empty(), "inactive temporal features leave native device creation unchanged");
	}

	void TestObservedColorContract()
	{
		using namespace cs::features;

		ColorMetadata observed{
			.resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
			.range = ColorRange::kFull,
			.transfer = TransferFunction::kGamma22,
			.primaries = ColorPrimaries::kUnspecified,
			.stage = ColorStage::kPostTonemapLut,
			.alpha = AlphaMode::kIgnored,
			.exposure = ExposureMode::kAutomatic
		};
		Check(
			IsFo4PostTonemapSdr(observed),
			"observed post-tonemap gamma/LUT input is admitted");
		observed.transfer = TransferFunction::kSRGB;
		Check(
			!IsFo4PostTonemapSdr(observed),
			"piecewise sRGB is not substituted for the observed gamma 2.2 transfer");
		observed.transfer = TransferFunction::kLinear;
		Check(
			!IsFo4PostTonemapSdr(observed),
			"UNORM storage does not imply linear input");
	}

	void TestFramePhasesAndRetries()
	{
		using namespace cs::render::temporal;

		FrameTransaction frame;
		Check(frame.Begin({ .realFrame = 8, .engineFrame = 80, .configurationRevision = 3, .slot = 1 }), "frame begins");
		Check(frame.Plan({ 1280, 720 }, { 1920, 1080 }), "frame plans");
		Check(frame.CommitRenderState({ 1280, 720 }), "render state commits");
		Check(frame.CaptureWorld(true), "world capture succeeds");
		Check(frame.ResolveScene(SceneResolution::kExternalPublished), "external scene resolves");
		Check(frame.Published(), "publication state is explicit");
		Check(frame.CapturePreUi(), "pre-UI capture succeeds");
		Check(frame.CaptureFinal(), "final capture succeeds");
		Check(frame.PreparePresent(true), "Present preparation succeeds");
		Check(frame.PresentAttempt(false, false, true), "retryable Present retains frame");
		Check(frame.Phase() == FramePhase::kPresentPrepared, "retry does not advance frame phase");
		Check(frame.PresentAttempts() == 1, "retry counts one attempt");
		Check(frame.PresentAttempt(false, true, false), "accepted retry succeeds");
		Check(frame.PresentAttempts() == 2, "accepted retry counts second attempt");
		Check(frame.Retire(), "accepted frame retires");
		Check(frame.Phase() == FramePhase::kRetired, "frame reaches retired state");
	}

	void TestOnceOnlyAndImmutableExtent()
	{
		using namespace cs::render::temporal;

		FrameTransaction frame;
		Check(frame.Begin({ .realFrame = 1 }), "frame begins");
		Check(frame.Plan({ 960, 540 }, { 1920, 1080 }), "frame plans");
		Check(!frame.CommitRenderState({ 1280, 720 }), "committed extent cannot differ from plan");
		Check(frame.Phase() == FramePhase::kFailed, "invalid extent fails the transaction");

		FrameTransaction duplicate;
		Check(duplicate.Begin({ .realFrame = 2 }), "second frame begins");
		Check(duplicate.Plan({ 960, 540 }, { 1920, 1080 }), "second frame plans");
		Check(duplicate.CommitRenderState({ 960, 540 }), "second frame commits");
		Check(duplicate.CaptureWorld(true), "second frame captures");
		Check(duplicate.ResolveScene(SceneResolution::kNativeCompleted), "native completion records");
		Check(!duplicate.ResolveScene(SceneResolution::kExternalPublished), "resolve is once-only");

		Check(
			duplicate.Begin({ .realFrame = 3, .slot = 0 }),
			"a failed slot can be reused by a later frame");
		Check(duplicate.Failure().empty(), "slot reuse clears the prior failure");
	}

	void TestPresentTestDoesNotConsumePacket()
	{
		using namespace cs::render::temporal;

		FrameTransaction frame;
		Check(frame.Begin({ .realFrame = 9, .slot = 1 }), "test frame begins");
		Check(frame.Plan({ 1920, 1080 }, { 1920, 1080 }), "test frame plans");
		Check(frame.CommitRenderState({ 1920, 1080 }), "test frame commits");
		Check(frame.CaptureWorld(true), "test frame captures");
		Check(frame.ResolveScene(SceneResolution::kNativeCompleted), "test frame resolves");
		Check(frame.CapturePreUi(), "test frame pre-UI capture succeeds");
		Check(frame.CaptureFinal(), "test frame final capture succeeds");
		Check(frame.PreparePresent(false), "test frame prepares once");
		Check(frame.PresentAttempt(true, true, false), "DXGI_PRESENT_TEST is observed");
		Check(
			frame.Phase() == FramePhase::kPresentPrepared,
			"DXGI_PRESENT_TEST does not consume the prepared packet");
		Check(frame.PresentAttempts() == 0, "DXGI_PRESENT_TEST is not a real Present attempt");
		Check(frame.PresentAttempt(false, true, false), "real Present remains accepted");
		Check(frame.Retire(), "tested packet retires after real acceptance");
	}

	void TestIndependentFrameSlots()
	{
		using namespace cs::render::temporal;

		FrameTransaction slots[2];
		Check(slots[0].Begin({ .realFrame = 10, .slot = 0 }), "slot zero begins");
		Check(slots[0].Plan({ 1280, 720 }, { 1920, 1080 }), "slot zero plans");
		Check(
			!slots[0].Begin({ .realFrame = 11, .slot = 0 }),
			"an in-flight slot cannot be reused");
		Check(
			slots[1].Begin({ .realFrame = 11, .slot = 1 }),
			"the other retained slot is independent");
		Check(slots[1].Identity().slot == 1, "slot identity is not a backbuffer alias");
	}

	void TestIndependentResetEpochs()
	{
		using namespace cs::render::temporal;

		ResetEpochs epochs;
		Check(epochs.SuperResolutionPending(), "SR reset starts pending");
		Check(epochs.FrameGenerationPending(), "FG reset starts pending");
		Check(epochs.ArmFrameGeneration(), "FG reset arms once");
		Check(!epochs.ArmFrameGeneration(), "FG reset is not re-issued before completion");
		Check(epochs.FrameGenerationRequested() == 1, "initial FG reset epoch is reported");
		Check(epochs.FrameGenerationConsumed() == 0, "initial FG reset is unconsumed");
		epochs.ConsumeSuperResolution(false);
		Check(epochs.SuperResolutionPending(), "failed SR work does not consume reset");
		epochs.ConsumeSuperResolution(true);
		Check(!epochs.SuperResolutionPending(), "successful SR work consumes reset");
		Check(epochs.FrameGenerationPending(), "SR consumption does not consume FG reset");
		epochs.RequestFrameGeneration();
		Check(epochs.ArmFrameGeneration(), "new FG epoch can be armed");
		epochs.ConsumeFrameGeneration(true);
		Check(!epochs.FrameGenerationPending(), "FG reset consumes independently");
		Check(
			epochs.FrameGenerationConsumed() == epochs.FrameGenerationRequested(),
			"FG reset epoch reports successful consumption");
	}

	void TestLatencyTimeline()
	{
		using namespace cs::render::temporal;

		LatencyTimeline timeline;
		Check(timeline.Frame() == 0, "latency timeline starts without a frame");
		Check(timeline.BeginFrame() == 1, "normal loop begins the first real frame");
		Check(
			timeline.Phase() == LatencyPhase::kSleep,
			"sleep precedes message-loop input and simulation");
		Check(timeline.BeginSimulation(), "simulation starts after the message loop");
		Check(
			timeline.EndSimulationAndBeginRenderSubmit(),
			"main-thread simulation end precedes render submission");
		Check(
			timeline.BeginPresent(false),
			"first real Present closes render submission");
		Check(
			timeline.RenderSubmitEnded(),
			"render submission ends exactly before the first Present attempt");
		Check(
			timeline.EndPresent(false, true),
			"retryable Present completes its attempt");
		Check(
			timeline.Phase() == LatencyPhase::kPresentRetry,
			"retryable Present retains the real-frame timeline");
		Check(
			timeline.BeginPresent(false),
			"same-frame Present retry is accepted");
		Check(
			timeline.EndPresent(false, false),
			"accepted Present completes the frame");
		Check(
			timeline.PresentAttempts() == 2,
			"Present retries do not create another real frame");
		Check(
			timeline.BeginPresent(true),
			"DXGI_PRESENT_TEST is observational");
		Check(
			timeline.EndPresent(true, false),
			"DXGI_PRESENT_TEST has no phase effect");
		Check(timeline.BeginFrame() == 2, "next normal loop advances once");
	}

	class MockSuperResolution final :
		public cs::render::temporal::ISuperResolutionProvider
	{
	public:
		const char* Name() const noexcept override { return "mock-sr"; }
		cs::render::temporal::ProviderResult Initialize(
			const cs::render::temporal::SuperResolutionInitContext&) override
		{
			initialized = true;
			return { .code = cs::render::temporal::ProviderResultCode::kSuccess };
		}
		cs::render::temporal::SuperResolutionSizeResult QueryRenderSize(
			const cs::render::temporal::SuperResolutionSizeRequest& a_request) override
		{
			if (const auto* cached = sizeCache.Find(a_request)) {
				return *cached;
			}
			++sizeQueryCount;
			cs::render::temporal::SuperResolutionSizeResult result{
				.result = {
					.code =
						cs::render::temporal::ProviderResultCode::kSuccess
				},
				.renderWidth = a_request.qualityMode == 0
					? a_request.outputWidth
					: a_request.outputWidth - 1,
				.renderHeight = a_request.qualityMode == 0
					? a_request.outputHeight
					: a_request.outputHeight - 1
			};
			sizeCache.Store(a_request, result);
			return result;
		}
		cs::render::temporal::ProviderResult Record(
			const cs::render::temporal::SuperResolutionRequest& a_request) override
		{
			recordedD3D12 =
				std::holds_alternative<
					cs::render::temporal::D3D12RecordingContext>(
					a_request.recording);
			return { .code = cs::render::temporal::ProviderResultCode::kSuccess };
		}
		void DestroyAfterDrain() noexcept override { initialized = false; }

		bool initialized = false;
		bool recordedD3D12 = false;
		std::uint32_t sizeQueryCount = 0;
		cs::render::temporal::SuperResolutionSizeCache sizeCache;
	};

	class MockFrameGeneration final :
		public cs::render::temporal::IFrameGenerationProvider
	{
	public:
		const char* Name() const noexcept override { return "mock-fg"; }
		cs::render::temporal::ProviderResult PrepareDevice(ID3D12Device**) override { return Success(); }
		cs::render::temporal::ProviderResult PrepareFactory(IDXGIFactory4**) override { return Success(); }
		cs::render::temporal::ProviderResult CreatePresentation(
			const cs::render::temporal::PresentationCreateContext&,
			IDXGISwapChain4**) override
		{
			created = true;
			return Success();
		}
		cs::render::temporal::ProviderResult CreateDisplayResources(
			std::uint32_t,
			std::uint32_t,
			DXGI_FORMAT,
			std::uint32_t) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult PrepareFrame(
			const cs::render::temporal::FrameGenerationRequest&) override
		{
			++prepareCount;
			return Success();
		}
		cs::render::temporal::ProviderResult SetGenerationEnabled(bool) override { return Success(); }
		cs::render::temporal::ProviderResult WaitForPresentInputs() override
		{
			++retirementCount;
			return Success();
		}
		cs::render::temporal::ProviderResult Sleep(std::uint32_t) override { return Success(); }
		cs::render::temporal::ProviderResult SetLatencyMarker(
			cs::render::temporal::LatencyMarker,
			std::uint32_t) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult Quiesce() override
		{
			quiesced = true;
			return Success();
		}
		void ReleaseDisplayResources() noexcept override { released = true; }
		void DestroyAfterDrain() noexcept override { destroyed = true; }
		bool IsReady() const noexcept override { return created; }

		static cs::render::temporal::ProviderResult Success()
		{
			return {
				.code =
					cs::render::temporal::ProviderResultCode::kSuccess
			};
		}

		bool created = false;
		bool quiesced = false;
		bool released = false;
		bool destroyed = false;
		std::uint32_t prepareCount = 0;
		std::uint32_t retirementCount = 0;
	};

	void TestTypedProviderContracts()
	{
		using namespace cs::render::temporal;

		ColorContract observed{
			.resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
			.range = ColorRange::kFull,
			.transfer = TransferFunction::kGamma22,
			.primaries = ColorPrimaries::kUnspecified,
			.stage = ColorStage::kPostTonemapLut,
			.alpha = AlphaMode::kIgnored,
			.exposure = ExposureMode::kAutomatic
		};
		Check(
			IsFo4PostTonemapSdr(observed),
			"observed gamma-2.2 post-LUT contract is accepted");
		observed.transfer = TransferFunction::kLinear;
		Check(
			!IsFo4PostTonemapSdr(observed),
			"linear intermediates cannot masquerade as engine gamma output");

		MockSuperResolution sr;
		SuperResolutionInitContext init{
			.device = static_cast<ID3D12Device*>(nullptr)
		};
		Check(sr.Initialize(init).Succeeded(), "mock SR initializes");
		const SuperResolutionSizeRequest nativeRequest{
			.outputWidth = 3840,
			.outputHeight = 2160,
			.qualityMode = 0
		};
		const auto nativeSize = sr.QueryRenderSize(nativeRequest);
		Check(
			nativeSize.Succeeded() &&
				nativeSize.renderWidth == nativeRequest.outputWidth &&
				nativeSize.renderHeight == nativeRequest.outputHeight,
			"Native AA preserves the exact output extent");
		(void)sr.QueryRenderSize(nativeRequest);
		Check(
			sr.sizeQueryCount == 1,
			"repeated provider sizing requests use the provider cache");
		(void)sr.QueryRenderSize({
			.outputWidth = 3840,
			.outputHeight = 2160,
			.qualityMode = 1
		});
		Check(
			sr.sizeQueryCount == 2,
			"quality changes invalidate the provider sizing cache");
		(void)sr.QueryRenderSize({
			.outputWidth = 2560,
			.outputHeight = 1440,
			.qualityMode = 1
		});
		Check(
			sr.sizeQueryCount == 3,
			"output changes invalidate the provider sizing cache");
		MockSuperResolution secondProvider;
		(void)secondProvider.QueryRenderSize(nativeRequest);
		Check(
			secondProvider.sizeQueryCount == 1 && sr.sizeQueryCount == 3,
			"provider sizing caches remain provider-owned");

		RenderSizeState sizing;
		sizing.SetNative(3441, 1441);
		const SuperResolutionSizeRequest oddRequest{
			.outputWidth = 3441,
			.outputHeight = 1441,
			.qualityMode = 1
		};
		const SuperResolutionSizeResult oddResult{
			.result = {
				.code = ProviderResultCode::kSuccess
			},
			.renderWidth = 2024,
			.renderHeight = 848
		};
		Check(
			sizing.SetRequested(oddRequest, oddResult),
			"valid queried extents are staged");
		Check(
			sizing.Committed() == RenderExtent{ 3441, 1441 },
			"queried extents remain uncommitted before preflight");
		sizing.CommitRequested();
		Check(
			sizing.Committed() == RenderExtent{ 2024, 848 },
			"preflight commit preserves exact queried integers");
		Check(
			static_cast<std::uint32_t>(
				static_cast<float>(oddRequest.outputWidth) *
				sizing.WidthRatio()) == oddResult.renderWidth &&
				static_cast<std::uint32_t>(
					static_cast<float>(oddRequest.outputHeight) *
					sizing.HeightRatio()) == oddResult.renderHeight,
			"published per-axis ratios reconstruct exact queried extents");
		const SuperResolutionSizeResult invalidResult{
			.result = {
				.code = ProviderResultCode::kFailure,
				.message = "query failed"
			}
		};
		Check(
			!sizing.SetRequested(oddRequest, invalidResult) &&
				sizing.Requested() == RenderExtent{ 3441, 1441 } &&
				sizing.Committed() == RenderExtent{ 3441, 1441 },
			"failed sizing restores native state before commitment");

		SuperResolutionRequest request{
			.recording = D3D12RecordingContext{}
		};
		Check(sr.Record(request).Succeeded(), "mock SR records");
		Check(sr.recordedD3D12, "typed SR request preserves D3D12 ownership");

		MockFrameGeneration fg;
		Check(
			fg.CreatePresentation({}, nullptr).Succeeded(),
			"mock FG creates presentation");
		Check(fg.PrepareFrame({}).Succeeded(), "mock FG prepares once");
		Check(fg.prepareCount == 1, "Present retry does not require another preparation");
		Check(
			fg.WaitForPresentInputs().Succeeded(),
			"vendor retirement is explicit before slot reuse");
		Check(fg.retirementCount == 1, "vendor retirement runs once");
		Check(fg.Quiesce().Succeeded(), "provider quiesces");
		fg.ReleaseDisplayResources();
		fg.DestroyAfterDrain();
		Check(
			fg.quiesced && fg.released && fg.destroyed,
			"provider cleanup order remains observable");
	}
}

int main()
{
	TestRequestedEffectiveAndPending();
	TestQuarantinedConfiguration();
	TestTemporalFeatureLevels();
	TestObservedColorContract();
	TestFramePhasesAndRetries();
	TestOnceOnlyAndImmutableExtent();
	TestPresentTestDoesNotConsumePacket();
	TestIndependentFrameSlots();
	TestIndependentResetEpochs();
	TestLatencyTimeline();
	TestTypedProviderContracts();
	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Temporal pipeline state checks passed\n";
	return 0;
}
