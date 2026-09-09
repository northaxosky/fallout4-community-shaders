#include "Render/TemporalPipelineState.h"
#include "Render/TemporalProvider.h"
#include "Render/TemporalRenderSizing.h"
#include "Render/TemporalDevicePolicy.h"
#include "Render/TemporalStartup.h"
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
		session.proxyInstalled = true;
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
		Check(state.Effective().superResolution == SuperResolutionMethod::kDLSS, "even an admitted SR change waits for restart");
		Check(!state.Effective().frameGenerationEnabled, "FG enablement switches live");
		Check(state.Effective().qualityMode == 2, "quality changes still apply to the effective provider");
		Check(state.Pending().required, "SR method changes uniformly require restart");

		state.SubmitLive(
			true,
			SuperResolutionMethod::kXeSS,
			2,
			true,
			FrameGenerationMethod::kDLSSG,
			6);
		Check(state.Effective().superResolution == SuperResolutionMethod::kDLSS, "unadmitted SR does not replace live provider");
		Check(state.Effective().frameGeneration == FrameGenerationMethod::kFSR3, "unadmitted FG does not replace live provider");
		Check(state.Pending().required, "unadmitted topology is pending restart");
		state.SubmitLive(true, SuperResolutionMethod::kDLSS, 2, true, FrameGenerationMethod::kFSR3, 6);
		Check(!state.Pending().required, "restoring both startup selections clears the pending change");

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
			state.Effective().superResolution == SuperResolutionMethod::kTAA,
			"selecting None after failure also waits for restart");
		Check(
			state.Pending().required,
			"native method selection does not erase the failure's restart request");
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
			session.proxyInstalled = true;
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
			"upscaling sessions use a consistent FSR-compatible device preference");
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

	void TestUniformStartupSelections()
	{
		using namespace cs::render::temporal;
		for (unsigned sr = 0; sr < static_cast<unsigned>(SuperResolutionMethod::kCount); ++sr) {
			for (unsigned fg = 0; fg < static_cast<unsigned>(FrameGenerationMethod::kCount); ++fg) {
				RequestedTopology request;
				request.upscalingEligible = true;
				request.frameGenerationEligible = true;
				request.superResolution = static_cast<SuperResolutionMethod>(sr);
				request.frameGeneration = static_cast<FrameGenerationMethod>(fg);
				TopologyState state;
				Check(state.Freeze(request), "matrix request freezes");
				SessionTopology session;
				session.valid = true;
				session.proxyInstalled = request.frameGeneration != FrameGenerationMethod::kOff;
				session.admittedSr.fill(true);
				session.admittedFg = request.frameGeneration;
				Check(state.Admit(session), "matrix session admits");
				for (unsigned nextSr = 0; nextSr < static_cast<unsigned>(SuperResolutionMethod::kCount); ++nextSr) {
					for (unsigned nextFg = 0; nextFg < static_cast<unsigned>(FrameGenerationMethod::kCount); ++nextFg) {
						const bool enabled = (nextSr + nextFg) % 2 == 0;
						const unsigned quality = (nextSr + nextFg) % 5;
						state.SubmitLive(
							enabled, static_cast<SuperResolutionMethod>(nextSr), quality,
							enabled, static_cast<FrameGenerationMethod>(nextFg), 10);
						Check(
							state.Effective().superResolution == request.superResolution &&
								state.Effective().frameGeneration == request.frameGeneration,
							"every SR/FG method combination remains fixed until restart");
						Check(
							state.Pending().required == (nextSr != sr || nextFg != fg),
							"restart policy is uniform, including None, TAA, and FG Off");
						Check(
							state.Effective().qualityMode == quality &&
								state.Effective().superResolutionEnabled == (enabled && sr != 0) &&
								state.Effective().frameGenerationEnabled == (enabled && fg != 0),
							"live controls affect the effective methods without changing them");
						Check(
							state.StartupRequest()->superResolution == request.superResolution &&
								state.StartupRequest()->frameGeneration == request.frameGeneration,
							"pending selections never rewrite the frozen startup request");
					}
				}
			}
		}

		TopologyState rejected;
		RequestedTopology request;
		request.upscalingEligible = true;
		request.frameGenerationEligible = true;
		Check(rejected.Freeze(request), "rejected session freezes");
		Check(rejected.Admit({}), "rejected session records failed admission");
		rejected.SubmitLive(true, SuperResolutionMethod::kFSR3, 1, true, FrameGenerationMethod::kXeSS, 2);
		Check(
			!rejected.Effective().superResolutionEnabled &&
				!rejected.Effective().frameGenerationEnabled,
			"live controls cannot activate a rejected startup session");

		TopologyState beforeAdmission;
		request.superResolution = SuperResolutionMethod::kDLSS;
		request.frameGeneration = FrameGenerationMethod::kXeSS;
		Check(beforeAdmission.Freeze(request), "pre-admission request freezes");
		beforeAdmission.SubmitLive(
			true, SuperResolutionMethod::kFSR3, 3,
			true, FrameGenerationMethod::kOff, 2);
		SessionTopology session;
		session.valid = true;
		session.proxyInstalled = true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kDLSS)] = true;
		session.admittedFg = FrameGenerationMethod::kXeSS;
		Check(beforeAdmission.Admit(session), "graphics creation uses the frozen request");
		Check(
			beforeAdmission.Effective().superResolution == SuperResolutionMethod::kDLSS &&
				beforeAdmission.Effective().frameGeneration == FrameGenerationMethod::kXeSS &&
				beforeAdmission.Pending().required,
			"UI edits before device creation cannot replace the frozen methods");
	}

	void TestSelectedStartupInitialization()
	{
		using namespace cs::render::temporal;
		std::vector<SuperResolutionMethod> calls;
		const auto available = [&](SuperResolutionMethod a_method) {
			calls.push_back(a_method);
			return ProviderResult{ .code = ProviderResultCode::kSuccess };
		};
		RequestedTopology request;
		request.upscalingEligible = true;
		for (unsigned sr = 0; sr < static_cast<unsigned>(SuperResolutionMethod::kCount); ++sr) {
			request.superResolution = static_cast<SuperResolutionMethod>(sr);
			calls.clear();
			const auto admission = InitializeSelectedSuperResolution(request, available);
			Check(admission.methods[sr] && admission.detail.empty(), "selected startup method is admitted");
			Check(
				sr < 2 ? calls.empty() : calls == std::vector{ request.superResolution },
				"startup initializes only the selected external provider, never standby providers");
		}
		request.upscalingEligible = false;
		calls.clear();
		const auto inactive = InitializeSelectedSuperResolution(request, available);
		Check(
			calls.empty() && std::ranges::none_of(inactive.methods, [](bool a_value) { return a_value; }),
			"an unloaded Upscaling feature initializes no SR providers");

		request.upscalingEligible = true;
		request.superResolution = SuperResolutionMethod::kDLSS;
		const auto dlssUnavailable = [&](SuperResolutionMethod a_method) {
			calls.push_back(a_method);
			return ProviderResult{
				.code = a_method == SuperResolutionMethod::kDLSS
					? ProviderResultCode::kUnavailable : ProviderResultCode::kSuccess,
				.message = a_method == SuperResolutionMethod::kDLSS ? "DLSS unavailable" : ""
			};
		};
		for (const auto fallback : {
				 SuperResolutionMethod::kNone, SuperResolutionMethod::kTAA,
				 SuperResolutionMethod::kFSR3, SuperResolutionMethod::kXeSS }) {
			request.noDlssFallback = fallback;
			calls.clear();
			const auto admission = InitializeSelectedSuperResolution(request, dlssUnavailable);
			const auto expected = fallback == SuperResolutionMethod::kNone || fallback == SuperResolutionMethod::kTAA
				? std::vector{ SuperResolutionMethod::kDLSS }
				: std::vector{ SuperResolutionMethod::kDLSS, fallback };
			Check(calls == expected, "a fallback initializes only after the requested DLSS runtime fails");
			Check(
				admission.methods[static_cast<std::size_t>(fallback)] &&
					admission.detail.contains("DLSS unavailable"),
				"startup fallback preserves an explicit explanation");
			TopologyState state;
			Check(state.Freeze(request), "fallback request freezes");
			SessionTopology session;
			session.valid = true;
			session.admittedSr = admission.methods;
			Check(state.Admit(session), "fallback admission records");
			state.SubmitLive(true, SuperResolutionMethod::kDLSS, 2, false, FrameGenerationMethod::kOff, 3);
			Check(
				state.Effective().superResolution == fallback && !state.Pending().required,
				"tuning does not replace a startup fallback or request a spurious restart");
			state.SubmitLive(true, fallback, 2, false, FrameGenerationMethod::kOff, 4);
			Check(
				state.Effective().superResolution == fallback && state.Pending().required,
				"changing the requested startup method is explicit even when it matches the fallback");
		}

		request.noDlssFallback = SuperResolutionMethod::kXeSS;
		calls.clear();
		const auto unavailable = InitializeSelectedSuperResolution(request, [&](SuperResolutionMethod a_method) {
			calls.push_back(a_method);
			return ProviderResult{ .code = ProviderResultCode::kUnavailable, .message = "unavailable" };
		});
		Check(
			calls == std::vector{ SuperResolutionMethod::kDLSS, SuperResolutionMethod::kXeSS } &&
				!unavailable.methods[static_cast<std::size_t>(SuperResolutionMethod::kXeSS)] &&
				unavailable.detail.contains("fallback is unavailable"),
			"failed fallback does not silently admit another provider");
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

	void TestPreUiHandoffPlanning()
	{
		using namespace cs::render::temporal;

		const auto fgOnly = PlanPreUiHandoff(true, true, false);
		Check(
			fgOnly.captureFrameGenerationInputs,
			"FG-only captures depth and motion at the pre-UI seam");
		Check(
			!fgOnly.driveSuperResolution,
			"FG-only does not enter SR-owned resolve state");
		Check(
			fgOnly.ShouldCaptureHudlessColor(false),
			"FG-only captures native pre-UI color without an SR publication");

		const auto srOnly = PlanPreUiHandoff(true, false, true);
		Check(
			!srOnly.captureFrameGenerationInputs,
			"SR-only does not capture frame-generation inputs");
		Check(
			srOnly.driveSuperResolution,
			"SR-only retains resolve, jitter, ratio, and TAA ownership");
		Check(
			!srOnly.ShouldCaptureHudlessColor(true),
			"SR-only does not publish an FG input packet");

		const auto combined = PlanPreUiHandoff(true, true, true);
		Check(
			combined.captureFrameGenerationInputs &&
				combined.driveSuperResolution,
			"combined SR and FG retain both sides of the handoff");
		Check(
			combined.ShouldCaptureHudlessColor(true),
			"combined SR and FG capture the successfully published pre-UI color");
		Check(
			!combined.ShouldCaptureHudlessColor(false),
			"failed SR publication cannot become an FG input packet");

		const auto inactive = PlanPreUiHandoff(true, false, false);
		Check(
			!inactive.captureFrameGenerationInputs &&
				!inactive.driveSuperResolution,
			"native and disabled temporal modes do no handoff work");
		const auto gammaOnly = PlanPreUiHandoff(false, true, true);
		Check(
			!gammaOnly.captureFrameGenerationInputs &&
				!gammaOnly.driveSuperResolution,
			"an unexpected Gamma-only seam cannot publish incomplete inputs");
	}

	void TestFrameGenerationActivity()
	{
		using namespace cs::render::temporal;

		FrameTransaction frame;
		Check(
			!IsFrameGenerationActive(true, true, true, 20, 200, frame),
			"ready configuration is not active before frame work");
		Check(frame.Begin({ .realFrame = 20, .engineFrame = 200 }), "activity frame begins");
		Check(frame.Plan({ 1920, 1080 }, { 1920, 1080 }), "activity frame plans");
		Check(frame.CommitRenderState({ 1920, 1080 }), "activity frame commits");
		Check(frame.CaptureWorld(true), "activity frame captures");
		Check(frame.ResolveScene(SceneResolution::kNativeCompleted), "activity frame resolves");
		Check(frame.CapturePreUi(), "activity frame captures pre-UI color");
		Check(frame.CaptureFinal(), "activity frame captures final color");
		Check(frame.PreparePresent(true), "activity frame prepares FG");
		Check(
			IsFrameGenerationActive(true, true, true, 20, 200, frame),
			"valid prepared FG work is active without a generated-frame counter");
		Check(
			!IsFrameGenerationActive(false, true, true, 20, 200, frame) &&
				!IsFrameGenerationActive(true, false, true, 20, 200, frame) &&
				!IsFrameGenerationActive(true, true, false, 20, 200, frame),
			"configured, effective, and ready remain independent activity gates");
		Check(
			IsFrameGenerationActive(true, true, true, 21, 201, frame) &&
				IsFrameGenerationActive(true, true, true, 25, 201, frame),
			"pre-UI telemetry observes the preceding engine frame despite intervening main-loop ticks");
		Check(
			!IsFrameGenerationActive(true, true, true, 21, 202, frame) &&
				!IsFrameGenerationActive(true, true, true, 19, 200, frame) &&
				!IsFrameGenerationActive(true, true, true, 20, 199, frame) &&
				!IsFrameGenerationActive(
					true, true, true, 20, std::nullopt, frame),
			"stale, future, or unavailable frame identities are inactive");
		Check(frame.PresentAttempt(false, true, false), "activity Present succeeds");
		Check(frame.Retire(), "activity frame retires");
		Check(
			IsFrameGenerationActive(true, true, true, 20, 200, frame),
			"accepted FG work remains observable through its frame boundary");

		TopologyState topology;
		RequestedTopology requested;
		requested.frameGenerationEligible = true;
		requested.frameGeneration = FrameGenerationMethod::kXeSS;
		Check(topology.Freeze(requested), "activity topology freezes");
		SessionTopology session;
		session.valid = true;
		session.proxyInstalled = true;
		session.admittedFg = FrameGenerationMethod::kXeSS;
		Check(topology.Admit(session), "activity topology admits");
		topology.SubmitLive(
			false, SuperResolutionMethod::kNone, 1,
			true, FrameGenerationMethod::kOff, 1);
		const auto& effective = topology.Effective();
		Check(
			topology.Pending().required &&
				IsFrameGenerationActive(
					effective.frameGeneration != FrameGenerationMethod::kOff,
					effective.frameGenerationEnabled, true, 20, 200, frame),
			"a pending Off selection does not hide work by the effective FG provider");

		FrameTransaction disabled;
		Check(disabled.Begin({ .realFrame = 30 }), "disabled frame begins");
		Check(disabled.Plan({ 1920, 1080 }, { 1920, 1080 }), "disabled frame plans");
		Check(disabled.CommitRenderState({ 1920, 1080 }), "disabled frame commits");
		Check(disabled.CaptureWorld(true), "disabled frame captures");
		Check(disabled.ResolveScene(SceneResolution::kNativeCompleted), "disabled frame resolves");
		Check(disabled.CapturePreUi(), "disabled frame captures pre-UI color");
		Check(disabled.CaptureFinal(), "disabled frame captures final color");
		Check(disabled.PreparePresent(false), "disabled frame prepares real-frame presentation");
		Check(
			!IsFrameGenerationActive(true, true, true, 30, 0, disabled),
			"a present prepared without FG work is not active");

		FrameTransaction failed;
		Check(failed.Begin({ .realFrame = 31 }), "failed frame begins");
		Check(failed.Plan({ 1920, 1080 }, { 1920, 1080 }), "failed frame plans");
		Check(failed.CommitRenderState({ 1920, 1080 }), "failed frame commits");
		Check(failed.CaptureWorld(true), "failed frame captures");
		Check(failed.ResolveScene(SceneResolution::kNativeCompleted), "failed frame resolves");
		Check(failed.CapturePreUi(), "failed frame captures pre-UI color");
		Check(failed.CaptureFinal(), "failed frame captures final color");
		Check(failed.PreparePresent(true), "failed frame prepares FG");
		Check(!failed.PresentAttempt(false, false, false), "failed Present marks the frame failed");
		Check(
			!IsFrameGenerationActive(true, true, true, 31, 0, failed),
			"failed prepared FG work is not active");
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
		cs::render::temporal::ProviderResult CancelFrame(
			const cs::render::temporal::FrameGenerationRequest&) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult SetGenerationEnabled(bool) override { return Success(); }
		cs::render::temporal::ProviderResult AcquirePresentInputs() override
		{
			++retirementCount;
			return Success();
		}
		cs::render::temporal::ProviderResult CollectPresentStatus(
			UINT,
			HRESULT) override { return Success(); }
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
		cs::render::temporal::ProviderResult ReleaseDisplayResources() noexcept override
		{
			released = true;
			return Success();
		}
		cs::render::temporal::ProviderResult DestroyAfterDrain() noexcept override
		{
			destroyed = true;
			return Success();
		}
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
			fg.AcquirePresentInputs().Succeeded(),
			"vendor retirement is explicit before slot reuse");
		Check(fg.retirementCount == 1, "vendor retirement runs once");
		Check(fg.Quiesce().Succeeded(), "provider quiesces");
		(void)fg.ReleaseDisplayResources();
		(void)fg.DestroyAfterDrain();
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
	TestUniformStartupSelections();
	TestSelectedStartupInitialization();
	TestObservedColorContract();
	TestFramePhasesAndRetries();
	TestPreUiHandoffPlanning();
	TestFrameGenerationActivity();
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
