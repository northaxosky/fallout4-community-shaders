#include "Render/TemporalDevicePolicy.h"
#include "Render/TemporalPipelineState.h"
#include "Render/TemporalPresentation.h"
#include "Render/TemporalProvider.h"
#include "Render/TemporalRenderSettings.h"
#include "Render/TemporalRenderSizing.h"
#include "Render/TemporalStartup.h"

#include <DearModdingUI/PresentationCore.h>

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

	bool Capture(cs::render::temporal::FrameTransaction& a_frame,
		std::uint64_t a_realFrame, std::uint64_t a_engineFrame = 0,
		std::uint32_t a_slot = 0)
	{
		return a_frame.Capture(
			{ .realFrame = a_realFrame, .engineFrame = a_engineFrame, .slot = a_slot },
			{ 1280, 720 }, { 1920, 1080 });
	}

	void TestRequestedEffectiveAndPending()
	{
		using namespace cs::render::temporal;

		const auto fgFailure = ClassifyFailure(FailureDomain::kFrameGeneration,
			SuperResolutionMethod::kDLSS,
			FrameGenerationMethod::kFSR3);
		Check(!fgFailure.superResolution && fgFailure.frameGeneration,
			"FG algorithm failure does not quarantine independent SR");
		const auto sharedFailure =
			ClassifyFailure(FailureDomain::kStreamline, SuperResolutionMethod::kDLSS,
				FrameGenerationMethod::kDLSSG);
		Check(sharedFailure.superResolution && sharedFailure.frameGeneration,
			"Streamline failure quarantines both dependent consumers");
		const auto unrelatedFailure =
			ClassifyFailure(FailureDomain::kStreamline, SuperResolutionMethod::kFSR3,
				FrameGenerationMethod::kFSR3);
		Check(unrelatedFailure.superResolution &&
				  unrelatedFailure.frameGeneration,
			"Streamline failure quarantines native FSR SR and FG consumers");
		const auto transportFailure =
			ClassifyFailure(FailureDomain::kTransport, SuperResolutionMethod::kFSR3,
				FrameGenerationMethod::kFSR3);
		Check(transportFailure.superResolution && transportFailure.frameGeneration,
			"shared transport failure stops both consumers");

		TopologyState state;
		RequestedTopology requested;
		requested.upscalingEligible = true;
		requested.frameGenerationEligible = true;
		requested.superResolution = SuperResolutionMethod::kDLSS;
		requested.frameGeneration = FrameGenerationMethod::kFSR3;
		requested.revision = 4;
		Check(state.Freeze(requested), "first request freezes");
		Check(!state.Freeze(requested), "request freezes exactly once");

		SessionTopology session;
		session.valid = true;
		session.proxyInstalled = true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kNone)] =
			true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kTAA)] =
			true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kFSR3)] =
			true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kDLSS)] =
			true;
		session.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kOff)] = true;
		session.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kFSR3)] = true;
		session.activeFg = FrameGenerationMethod::kFSR3;
		Check(state.Admit(session), "session admits after request");
		Check(state.Effective().superResolution == SuperResolutionMethod::kDLSS,
			"requested DLSS is effective");
		Check(state.Effective().frameGeneration == FrameGenerationMethod::kFSR3,
			"requested FSR FG is effective");

		state.FailSuperResolutionToNative(
			7, "external provider failed after render commitment");
		Check(state.Effective().superResolution == SuperResolutionMethod::kTAA,
			"runtime SR failure switches to native TAA");
		Check(state.Effective().revision == 7,
			"runtime SR failure advances the effective revision");
		Check(state.Pending().required,
			"runtime SR failure requires restart before external SR can resume");
		state.SubmitLive(true, SuperResolutionMethod::kDLSS, 1, false,
			FrameGenerationMethod::kFSR3, 8);
		Check(state.Effective().superResolution == SuperResolutionMethod::kTAA,
			"live configuration cannot re-enable a failed external SR session");
		Check(state.Request()->superResolution == SuperResolutionMethod::kDLSS,
			"requested topology reports the failed live selection");
		state.SubmitLive(true, SuperResolutionMethod::kNone, 1, false,
			FrameGenerationMethod::kFSR3, 9);
		Check(state.Effective().superResolution == SuperResolutionMethod::kTAA,
			"selecting None after failure also waits for restart");
		Check(state.Pending().required,
			"native method selection does not erase the failure's restart request");
		state.SubmitLive(true, SuperResolutionMethod::kFSR3, 1, false,
			FrameGenerationMethod::kFSR3, 10);
		Check(state.Request()->superResolution == SuperResolutionMethod::kFSR3 &&
				  state.Effective().superResolution == SuperResolutionMethod::kTAA,
			"requested and effective topology remain distinct after failure");
	}

	void TestInvalidProviderValues()
	{
		using namespace cs::render::temporal;

		constexpr auto removedSr =
			static_cast<SuperResolutionMethod>(kMaxUpscaleMethodValue + 1);
		constexpr auto removedFg =
			static_cast<FrameGenerationMethod>(kMaxFrameGenerationMethodValue + 1);
		SessionTopology session;
		Check(static_cast<std::size_t>(removedSr) >= session.admittedSr.size(),
			"removed SR value cannot index the supported provider array");

		RequestedTopology requested;
		requested.upscalingEligible = true;
		requested.superResolutionEnabled = true;
		requested.superResolution = removedSr;
		requested.frameGenerationEligible = true;
		requested.frameGenerationEnabled = true;
		requested.frameGeneration = removedFg;
		TopologyState state;
		Check(state.Freeze(requested),
			"removed-value topology freezes for rejection");
		session.valid = true;
		session.proxyInstalled = true;
		session.admittedSr.fill(true);
		session.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kOff)] = true;
		session.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kFSR3)] = true;
		Check(state.Admit(session), "removed-value topology reaches safe admission");
		Check(state.Effective().superResolution == SuperResolutionMethod::kNone &&
				  !state.Effective().superResolutionEnabled,
			"removed SR value is rejected instead of selecting another provider");
		Check(state.Effective().frameGeneration == FrameGenerationMethod::kOff &&
				  !state.Effective().frameGenerationEnabled,
			"removed FG value is rejected instead of selecting another provider");
	}

	void TestQuarantinedConfiguration()
	{
		using namespace cs::render::temporal;
		for (const auto domain :
			{ FailureDomain::kSuperResolution, FailureDomain::kFrameGeneration,
				FailureDomain::kEngine, FailureDomain::kTransport,
				FailureDomain::kStreamline }) {
			TopologyState state;
			RequestedTopology requested;
			requested.upscalingEligible = true;
			requested.frameGenerationEligible = true;
			requested.superResolution = SuperResolutionMethod::kDLSS;
			requested.frameGeneration = FrameGenerationMethod::kDLSSG;
			Check(state.Freeze(requested), "failure fixture freezes");
			SessionTopology session;
			session.valid = true;
			session.proxyInstalled = true;
			session.admittedSr.fill(true);
			session.admittedFg[static_cast<std::size_t>(
				FrameGenerationMethod::kOff)] = true;
			session.admittedFg[static_cast<std::size_t>(
				FrameGenerationMethod::kDLSSG)] = true;
			session.activeFg = FrameGenerationMethod::kDLSSG;
			Check(state.Admit(session), "failure fixture admits");

			const auto impact = ClassifyFailure(domain, requested.superResolution,
				requested.frameGeneration);
			state.Quarantine(impact, 11, "provider preflight failed");
			Check(state.Request()->superResolution == requested.superResolution &&
					  state.Request()->frameGeneration == requested.frameGeneration,
				"quarantine preserves the user's requested providers");
			Check(state.Effective().superResolutionEnabled == !impact.superResolution &&
					  state.Effective().frameGenerationEnabled ==
						  !impact.frameGeneration,
				"effective enablement reflects the quarantined consumers");
			Check(state.Effective().superResolution ==
						  (impact.superResolution ? SuperResolutionMethod::kNone : requested.superResolution) &&
					  state.Effective().frameGeneration ==
						  (impact.frameGeneration ? FrameGenerationMethod::kOff : requested.frameGeneration),
				"quarantined providers are not reported as effective");
			Check(state.Effective().revision == 11, "quarantine advances the revision");
			Check(state.Pending().required &&
					  state.Pending().reason.contains("provider preflight failed"),
				"quarantine exposes its actual cause and restart requirement");

			state.SubmitLive(true, requested.superResolution, 3, true,
				requested.frameGeneration, 12);
			Check(state.Effective().superResolutionEnabled == !impact.superResolution &&
					  state.Effective().frameGenerationEnabled ==
						  !impact.frameGeneration,
				"live settings cannot reactivate quarantined consumers");
			Check(state.Pending().required &&
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
		request.superResolution = SuperResolutionMethod::kFSR3;
		request.frameGenerationEligible = true;
		request.frameGeneration = FrameGenerationMethod::kFSR3;
		std::vector<D3D_FEATURE_LEVEL> levels{ D3D_FEATURE_LEVEL_11_0 };
		ConfigureTemporalFeatureLevels(request, levels);
		Check(levels == std::vector<D3D_FEATURE_LEVEL>{ D3D_FEATURE_LEVEL_11_1,
							D3D_FEATURE_LEVEL_11_0 },
			"temporal sessions request the shared-fence interop feature level");
		const auto once = levels;
		ConfigureTemporalFeatureLevels(request, levels);
		Check(levels == once, "feature-level preference is idempotent");

		levels = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_1 };
		ConfigureTemporalFeatureLevels(request, levels);
		Check(levels == std::vector<D3D_FEATURE_LEVEL>{ D3D_FEATURE_LEVEL_12_1,
							D3D_FEATURE_LEVEL_11_1,
							D3D_FEATURE_LEVEL_11_0 },
			"higher feature levels retain priority and lower levels remain "
			"fallbacks");

		levels.clear();
		ConfigureTemporalFeatureLevels(request, levels);
		Check(levels.size() == 7 && levels.front() == D3D_FEATURE_LEVEL_11_1 &&
				  levels[1] == D3D_FEATURE_LEVEL_11_0 &&
				  levels.back() == D3D_FEATURE_LEVEL_9_1,
			"an empty engine list preserves D3D11's default fallback levels");

		request.upscalingEligible = false;
		levels = { D3D_FEATURE_LEVEL_11_0 };
		ConfigureTemporalFeatureLevels(request, levels);
		Check(levels.front() == D3D_FEATURE_LEVEL_11_1,
			"frame generation alone preserves the shared interop requirement");

		request.frameGenerationEligible = false;
		levels.clear();
		ConfigureTemporalFeatureLevels(request, levels);
		Check(levels.empty(),
			"inactive temporal features leave native device creation unchanged");
	}

	void TestTemporalAvailabilityPresentation()
	{
		using namespace cs::render;
		using namespace cs::render::temporal;

		TemporalPipelineStatus status;
		auto checking = presentation::Describe(
			SuperResolutionMethod::kFSR4,
			status,
			{});
		Check(
			checking.kind ==
					presentation::AvailabilityKind::kChecking &&
				checking.reason == "Checking availability",
			"unknown capability state is presented as checking rather than unsupported");

		status.requestFrozen = true;
		status.d3d11Ready = true;
		status.session.valid = true;
		FidelityFXCapabilities fidelityFx;
		fidelityFx.fsr4SuperResolution.availability =
			CapabilityAvailability::kUnsupported;
		fidelityFx.fsr4SuperResolution.unavailableReason = 2;
		const auto unavailable = presentation::Describe(
			SuperResolutionMethod::kFSR4,
			status,
			fidelityFx);
		Check(
			unavailable.kind ==
					presentation::AvailabilityKind::kUnavailable &&
				unavailable.reason.contains("operating system"),
			"cached provider reasons are translated to plain language");

		const dmui::ChoiceOption<std::uint32_t> disabled{
			4,
			presentation::OptionLabel("FSR 4", unavailable),
			"fsr-4",
			unavailable.Selectable()
		};
		const auto activation =
			dmui::presentation_detail::ResolveChoiceActivation(
				1u,
				disabled,
				true);
		Check(
			!activation.changed && !activation.selected,
			"a capability-disabled temporal choice cannot activate");
	}

	void TestLiveTransitions()
	{
		using namespace cs::render::temporal;
		RequestedTopology liveRequest;
		liveRequest.upscalingEligible = true;
		liveRequest.frameGenerationEligible = true;
		liveRequest.superResolution = SuperResolutionMethod::kTAA;
		liveRequest.frameGeneration = FrameGenerationMethod::kOff;
		TopologyState live;
		Check(live.Freeze(liveRequest), "live transition fixture freezes");
		SessionTopology liveSession;
		liveSession.valid = true;
		liveSession.proxyInstalled = true;
		liveSession.admittedSr.fill(true);
		liveSession.admittedFg.fill(true);
		Check(live.Admit(liveSession), "live transition fixture admits");
		live.SubmitLive(true, SuperResolutionMethod::kDLSS, 2, false,
			FrameGenerationMethod::kOff, 10);
		Check(live.PendingTransition() &&
				  live.PendingTransition()->superResolution ==
					  SuperResolutionMethod::kDLSS &&
				  live.Effective().superResolution ==
					  SuperResolutionMethod::kTAA &&
				  !live.Pending().required,
			"native-to-DLSS queues an atomic live transition without changing "
			"the effective method early");
		Check(live.CommitPendingTransition(10) &&
				  live.Effective().superResolution ==
					  SuperResolutionMethod::kDLSS &&
				  live.Effective().qualityMode == 2,
			"successful live transition commits method and quality together");
		live.SubmitLive(true, SuperResolutionMethod::kNone, 2, false,
			FrameGenerationMethod::kOff, 11);
		Check(live.CommitPendingTransition(11) &&
				  live.Effective().superResolution ==
					  SuperResolutionMethod::kNone,
			"DLSS-to-native bypass commits without replacing presentation");
		live.SubmitLive(true, SuperResolutionMethod::kFSR3, 2, false,
			FrameGenerationMethod::kOff, 12);
		Check(live.PendingTransition() &&
				  live.PendingTransition()->superResolution ==
					  SuperResolutionMethod::kFSR3 &&
				  !live.Pending().required,
			"native FSR queues through the existing frame-boundary SR "
			"transition");
		Check(live.CommitPendingTransition(12) &&
				  live.Effective().superResolution ==
					  SuperResolutionMethod::kFSR3,
			"native FSR commits without replacing presentation");
		live.SubmitLive(
			true, SuperResolutionMethod::kDLSS, 1, false,
			FrameGenerationMethod::kOff, 13);
		Check(live.PendingTransition() &&
				  !live.Pending().required &&
				  live.CommitPendingTransition(13) &&
				  live.Effective().superResolution ==
					  SuperResolutionMethod::kDLSS,
			"native FSR and DLSS switch through one admitted SR seam");

		TopologyState mixedFsr4;
		Check(mixedFsr4.Freeze(liveRequest),
			"mixed FSR 4 fixture freezes");
		Check(mixedFsr4.Admit(liveSession),
			"mixed FSR 4 fixture admits");
		mixedFsr4.SubmitLive(
			true, SuperResolutionMethod::kDLSS, 1, true,
			FrameGenerationMethod::kFSR4, 130);
		Check(mixedFsr4.PendingTransition() &&
				  mixedFsr4.CommitPendingTransition(
					  130, FrameGenerationMethod::kFSR4) &&
				  mixedFsr4.Effective().superResolution ==
					  SuperResolutionMethod::kDLSS &&
				  mixedFsr4.Effective().frameGeneration ==
					  FrameGenerationMethod::kFSR4,
			"FSR 4 ML frame generation can be selected independently with "
			"DLSS super resolution");
		mixedFsr4.SubmitLive(
			true, SuperResolutionMethod::kFSR4, 1, true,
			FrameGenerationMethod::kDLSSG, 131);
		Check(mixedFsr4.PendingTransition() &&
				  mixedFsr4.CommitPendingTransition(
					  131, FrameGenerationMethod::kDLSSG) &&
				  mixedFsr4.Effective().superResolution ==
					  SuperResolutionMethod::kFSR4 &&
				  mixedFsr4.Effective().frameGeneration ==
					  FrameGenerationMethod::kDLSSG,
			"FSR 4 super resolution can be selected independently with "
			"DLSS-G");

		live.SubmitLive(true, SuperResolutionMethod::kNone, 2, true,
			FrameGenerationMethod::kDLSSG, 14);
		Check(live.PendingTransition() &&
				  live.PendingTransition()->frameGeneration ==
					  FrameGenerationMethod::kDLSSG &&
				  live.PendingTransition()->frameGenerationEnabled &&
				  !live.Pending().required,
			"an admitted frame-generation provider queues a live chain "
			"replacement");
		Check(live.CommitPendingTransition(
				  14, FrameGenerationMethod::kDLSSG) &&
				  live.Effective().frameGeneration ==
					  FrameGenerationMethod::kDLSSG &&
				  live.Effective().frameGenerationEnabled &&
				  live.Session()->activeFg ==
					  FrameGenerationMethod::kDLSSG,
			"provider activation commits with the actual private-chain owner");
		live.SubmitLive(true, SuperResolutionMethod::kNone, 2, true,
			FrameGenerationMethod::kFSR3, 15);
		Check(live.PendingTransition() &&
				  !live.Pending().required &&
				  live.CommitPendingTransition(
					  15, FrameGenerationMethod::kFSR3) &&
				  live.Effective().frameGeneration ==
					  FrameGenerationMethod::kFSR3 &&
				  live.Session()->activeFg ==
					  FrameGenerationMethod::kFSR3,
			"an admitted vendor switch commits atomically");
		live.SubmitLive(true, SuperResolutionMethod::kNone, 2, false,
			FrameGenerationMethod::kFSR3, 16);
		Check(live.PendingTransition() &&
				  live.CommitPendingTransition(
					  16, FrameGenerationMethod::kOff) &&
				  !live.Effective().frameGenerationEnabled &&
				  live.Effective().frameGeneration ==
					  FrameGenerationMethod::kFSR3 &&
				  live.Session()->activeFg ==
					  FrameGenerationMethod::kOff &&
				  !live.Pending().required,
			"explicit disable retains the selected vendor while committing a "
			"plain private chain");
		live.SubmitLive(true, SuperResolutionMethod::kNone, 2, true,
			FrameGenerationMethod::kFSR3, 17);
		Check(live.PendingTransition() &&
				  live.CommitPendingTransition(
					  17, FrameGenerationMethod::kFSR3) &&
				  live.Effective().frameGenerationEnabled,
			"re-enable recreates the retained admitted provider");

		TopologyState coalesced;
		Check(coalesced.Freeze(liveRequest),
			"coalesced transition fixture freezes");
		Check(coalesced.Admit(liveSession),
			"coalesced transition fixture admits");
		coalesced.SubmitLive(
			true, SuperResolutionMethod::kDLSS, 2, true,
			FrameGenerationMethod::kFSR3, 17);
		const auto firstTransition = coalesced.BeginPendingTransition();
		Check(firstTransition &&
				  firstTransition->frameGeneration ==
					  FrameGenerationMethod::kFSR3,
			"the frame boundary owns one immutable transition revision");
		coalesced.SubmitLive(
			true, SuperResolutionMethod::kTAA, 1, false,
			FrameGenerationMethod::kFSR3, 18);
		Check(coalesced.PendingTransition() &&
				  coalesced.PendingTransition()->revision == 18 &&
				  coalesced.PendingTransition()->superResolution ==
					  SuperResolutionMethod::kTAA &&
				  !coalesced.PendingTransition()->frameGenerationEnabled,
			"a newer request is compared with the in-flight target and retained");
		Check(coalesced.CommitPendingTransition(
				  17, FrameGenerationMethod::kFSR3) &&
				  coalesced.Effective().superResolution ==
					  SuperResolutionMethod::kDLSS &&
				  !coalesced.Effective().frameGenerationEnabled &&
				  coalesced.Session()->activeFg ==
					  FrameGenerationMethod::kFSR3 &&
				  coalesced.PendingTransition() &&
				  coalesced.PendingTransition()->revision == 18,
			"committing completed GPU work preserves a newer coalesced request");
		const auto secondTransition = coalesced.BeginPendingTransition();
		Check(secondTransition &&
				  coalesced.CommitPendingTransition(
					  18, FrameGenerationMethod::kOff) &&
				  coalesced.Effective().superResolution ==
					  SuperResolutionMethod::kTAA &&
				  !coalesced.Effective().frameGenerationEnabled &&
				  coalesced.Session()->activeFg ==
					  FrameGenerationMethod::kOff,
			"the newer coalesced request commits on the next frame boundary");

		TopologyState deferred;
		Check(deferred.Freeze(liveRequest),
			"deferred transition fixture freezes");
		Check(deferred.Admit(liveSession),
			"deferred transition fixture admits");
		deferred.SubmitLive(
			true, SuperResolutionMethod::kDLSS, 2, true,
			FrameGenerationMethod::kDLSSG, 19);
		Check(deferred.BeginPendingTransition().has_value(),
			"retryable presentation work claims the pending revision");
		deferred.DeferTransition(19);
		Check(deferred.PendingTransition() &&
				  deferred.PendingTransition()->revision == 19 &&
				  deferred.Effective().superResolution ==
					  SuperResolutionMethod::kTAA,
			"retry deferral restores the same transaction without an early "
			"effective-state change");
		Check(deferred.BeginPendingTransition().has_value(),
			"deferred transaction can be retried");
		deferred.SubmitLive(
			true, SuperResolutionMethod::kFSR3, 2, false,
			FrameGenerationMethod::kOff, 20);
		deferred.DeferTransition(19);
		Check(deferred.PendingTransition() &&
				  deferred.PendingTransition()->revision == 20 &&
				  deferred.PendingTransition()->superResolution ==
					  SuperResolutionMethod::kFSR3,
			"a retry deferral never overwrites a newer coalesced request");

		TopologyState fallback;
		Check(fallback.Freeze(liveRequest),
			"frame-generation fallback fixture freezes");
		Check(fallback.Admit(liveSession),
			"frame-generation fallback fixture admits");
		fallback.SubmitLive(
			true, SuperResolutionMethod::kTAA, 1, true,
			FrameGenerationMethod::kDLSSG, 20);
		Check(fallback.CommitPendingFrameGenerationFallback(
				  20, "target chain creation failed") &&
				  fallback.Effective().superResolution ==
					  SuperResolutionMethod::kTAA &&
				  fallback.Effective().frameGeneration ==
					  FrameGenerationMethod::kOff &&
				  !fallback.Effective().frameGenerationEnabled &&
				  fallback.Session()->activeFg ==
					  FrameGenerationMethod::kOff &&
				  fallback.Pending().required &&
				  fallback.Pending().reason.contains(
					  "target chain creation failed"),
			"post-teardown target failure commits the safe plain fallback "
			"without discarding the valid SR selection");

		TopologyState unavailableFg;
		Check(unavailableFg.Freeze(liveRequest),
			"unavailable FG fixture freezes");
		SessionTopology oneProvider = liveSession;
		oneProvider.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kFSR3)] = false;
		Check(unavailableFg.Admit(oneProvider),
			"unavailable FG fixture admits its supported provider");
		unavailableFg.SubmitLive(
			true, SuperResolutionMethod::kTAA, 1, true,
			FrameGenerationMethod::kFSR3, 21);
		Check(!unavailableFg.PendingTransition() &&
				  unavailableFg.Pending().required &&
				  unavailableFg.Effective().frameGeneration ==
					  FrameGenerationMethod::kOff,
			"an unavailable provider is rejected before private-chain "
			"teardown");

		TopologyState unavailableFsr4Fg;
		Check(unavailableFsr4Fg.Freeze(liveRequest),
			"unavailable FSR 4 FG fixture freezes");
		oneProvider.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kFSR4)] = false;
		Check(unavailableFsr4Fg.Admit(oneProvider),
			"unavailable FSR 4 FG fixture admits its working providers");
		unavailableFsr4Fg.SubmitLive(
			true, SuperResolutionMethod::kTAA, 1, true,
			FrameGenerationMethod::kFSR4, 211);
		Check(!unavailableFsr4Fg.PendingTransition() &&
				  unavailableFsr4Fg.Pending().required &&
				  unavailableFsr4Fg.Effective().frameGeneration ==
					  FrameGenerationMethod::kOff,
			"an unavailable FSR 4 MLFG request preserves the working plain "
			"presentation chain");

		TopologyState unavailableSr;
		Check(unavailableSr.Freeze(liveRequest),
			"unavailable SR fixture freezes");
		oneProvider.admittedSr[static_cast<std::size_t>(
			SuperResolutionMethod::kFSR3)] = false;
		Check(unavailableSr.Admit(oneProvider),
			"unavailable SR fixture admits its supported providers");
		unavailableSr.SubmitLive(
			true, SuperResolutionMethod::kFSR3, 1, false,
			FrameGenerationMethod::kOff, 22);
		Check(!unavailableSr.PendingTransition() &&
				  unavailableSr.Pending().required &&
				  unavailableSr.Effective().superResolution ==
					  SuperResolutionMethod::kTAA,
			"an unavailable external SR provider is rejected before teardown");

		TopologyState unavailableFsr4Sr;
		Check(unavailableFsr4Sr.Freeze(liveRequest),
			"unavailable FSR 4 SR fixture freezes");
		oneProvider.admittedSr[static_cast<std::size_t>(
			SuperResolutionMethod::kFSR4)] = false;
		Check(unavailableFsr4Sr.Admit(oneProvider),
			"unavailable FSR 4 SR fixture admits its working providers");
		unavailableFsr4Sr.SubmitLive(
			true, SuperResolutionMethod::kFSR4, 1, false,
			FrameGenerationMethod::kOff, 221);
		Check(!unavailableFsr4Sr.PendingTransition() &&
				  unavailableFsr4Sr.Pending().required &&
				  unavailableFsr4Sr.Effective().superResolution ==
					  SuperResolutionMethod::kTAA,
			"an unavailable FSR 4 SR request preserves the previous working "
			"super-resolution method");

		TopologyState rejected;
		RequestedTopology request;
		request.upscalingEligible = true;
		request.frameGenerationEligible = true;
		Check(rejected.Freeze(request), "rejected session freezes");
		Check(rejected.Admit({}), "rejected session records failed admission");
		rejected.SubmitLive(true, SuperResolutionMethod::kFSR3, 1, true,
			FrameGenerationMethod::kDLSSG, 2);
		Check(!rejected.Effective().superResolutionEnabled &&
				  !rejected.Effective().frameGenerationEnabled,
			"live controls cannot activate a rejected startup session");

		TopologyState beforeAdmission;
		request.superResolution = SuperResolutionMethod::kDLSS;
		request.frameGeneration = FrameGenerationMethod::kDLSSG;
		Check(beforeAdmission.Freeze(request), "pre-admission request freezes");
		beforeAdmission.SubmitLive(true, SuperResolutionMethod::kFSR3, 3, true,
			FrameGenerationMethod::kOff, 2);
		SessionTopology session;
		session.valid = true;
		session.proxyInstalled = true;
		session.admittedSr[static_cast<std::size_t>(SuperResolutionMethod::kDLSS)] =
			true;
		session.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kOff)] = true;
		session.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kDLSSG)] = true;
		session.activeFg = FrameGenerationMethod::kDLSSG;
		Check(beforeAdmission.Admit(session),
			"graphics creation uses the frozen request");
		Check(beforeAdmission.Effective().superResolution ==
					  SuperResolutionMethod::kDLSS &&
				  beforeAdmission.Effective().frameGeneration ==
					  FrameGenerationMethod::kDLSSG &&
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
		for (unsigned sr = 0;
			sr < static_cast<unsigned>(SuperResolutionMethod::kCount); ++sr) {
			request.superResolution = static_cast<SuperResolutionMethod>(sr);
			calls.clear();
			const auto admission =
				InitializeSelectedSuperResolution(request, available);
			Check(admission.methods[sr] && admission.detail.empty(),
				"selected startup method is admitted");
			Check(sr < 2 ? calls.empty() : calls == std::vector{ request.superResolution },
				"startup initializes only the selected external provider, never "
				"standby providers");
		}
		request.upscalingEligible = false;
		calls.clear();
		const auto inactive = InitializeSelectedSuperResolution(request, available);
		Check(calls.empty() &&
				  std::ranges::none_of(inactive.methods,
					  [](bool a_value) { return a_value; }),
			"an unloaded Upscaling feature initializes no SR providers");

		request.upscalingEligible = true;
		for (const auto unavailableMethod :
			{ SuperResolutionMethod::kDLSS,
				SuperResolutionMethod::kFSR3,
				SuperResolutionMethod::kFSR4 }) {
			request.superResolution = unavailableMethod;
			calls.clear();
			const auto admission = InitializeSelectedSuperResolution(
				request,
				[&](SuperResolutionMethod a_method) {
					calls.push_back(a_method);
					return ProviderResult{
						.code = ProviderResultCode::kUnavailable,
						.message = "provider unavailable"
					};
				});
			Check(
				calls == std::vector{ unavailableMethod } &&
					admission.methods[static_cast<std::size_t>(
						SuperResolutionMethod::kTAA)] &&
					admission.detail.contains("Native TAA"),
				"an unavailable external startup method falls back only to native TAA");
			TopologyState fallback;
			Check(fallback.Freeze(request), "native startup fallback request freezes");
			SessionTopology session;
			session.valid = true;
			session.admittedSr = admission.methods;
			Check(fallback.Admit(session), "native startup fallback admits");
			Check(
				fallback.Effective().superResolution ==
						SuperResolutionMethod::kTAA &&
					fallback.Effective().superResolutionEnabled,
				"every unavailable external startup provider resolves to native TAA");
		}

		request.superResolution = SuperResolutionMethod::kNone;
		calls.clear();
		const auto nativeOff =
			InitializeSelectedSuperResolution(request, available);
		TopologyState off;
		Check(off.Freeze(request), "native Off request freezes");
		SessionTopology offSession;
		offSession.valid = true;
		offSession.admittedSr = nativeOff.methods;
		Check(off.Admit(offSession), "native Off request admits");
		Check(
			calls.empty() &&
				off.Effective().superResolution ==
					SuperResolutionMethod::kNone &&
				!off.Effective().superResolutionEnabled,
			"native Off remains Off without initializing or selecting a fallback");
	}

	void TestPreUiHandoffPlanning()
	{
		using namespace cs::render::temporal;
		struct Case
		{
			bool fullEffects;
			bool frameGeneration;
			bool superResolution;
			bool resolved;
			PreUiHandoffPlan expected;
			bool captureHudless;
		};
		for (const auto test :
			std::array{ Case{ true, true, false, false, { true, false }, true },
				Case{ true, false, true, true, { false, true }, false },
				Case{ true, true, true, true, { true, true }, true },
				Case{ true, true, true, false, { true, true }, false },
				Case{ true, false, false, false, {}, false },
				Case{ false, true, true, true, {}, false } }) {
			const auto plan = PlanPreUiHandoff(test.fullEffects, test.frameGeneration,
				test.superResolution);
			Check(plan.captureFrameGenerationInputs ==
						  test.expected.captureFrameGenerationInputs &&
					  plan.driveSuperResolution == test.expected.driveSuperResolution &&
					  plan.ShouldCaptureHudlessColor(test.resolved) ==
						  test.captureHudless,
				"pre-UI handoff preserves capture and resolve ownership");
		}
	}

	void TestFrameGenerationActivity()
	{
		using namespace cs::render::temporal;

		FrameTransaction frame;
		Check(!IsFrameGenerationActive(true, true, true, 20, 200, frame),
			"ready configuration is not active before frame work");
		Check(Capture(frame, 20, 200) && frame.PreparePresent(),
			"activity frame is captured and prepared");
		frame.SetFrameGenerationPrepared(true);
		Check(IsFrameGenerationActive(true, true, true, 20, 200, frame),
			"valid prepared FG work is active without a generated-frame counter");
		struct ActivityCase
		{
			bool configured;
			bool enabled;
			bool ready;
			std::uint64_t realFrame;
			std::optional<std::uint64_t> engineFrame;
			bool active;
		};
		for (const auto test :
			std::array{ ActivityCase{ false, true, true, 20, 200, false },
				ActivityCase{ true, false, true, 20, 200, false },
				ActivityCase{ true, true, false, 20, 200, false },
				ActivityCase{ true, true, true, 21, 201, true },
				ActivityCase{ true, true, true, 25, 201, true },
				ActivityCase{ true, true, true, 21, 202, false },
				ActivityCase{ true, true, true, 19, 200, false },
				ActivityCase{ true, true, true, 20, 199, false },
				ActivityCase{ true, true, true, 20, std::nullopt, false } }) {
			Check(IsFrameGenerationActive(test.configured, test.enabled, test.ready,
					  test.realFrame, test.engineFrame,
					  frame) == test.active,
				"FG activity requires current prepared work and all runtime gates");
		}
		Check(frame.PresentAttempt(false, true, false), "activity Present succeeds");
		Check(frame.Retire(), "activity frame retires");
		Check(IsFrameGenerationActive(true, true, true, 20, 200, frame),
			"accepted FG work remains observable through its frame boundary");

		TopologyState topology;
		RequestedTopology requested;
		requested.frameGenerationEligible = true;
		requested.frameGeneration = FrameGenerationMethod::kDLSSG;
		Check(topology.Freeze(requested), "activity topology freezes");
		SessionTopology session;
		session.valid = true;
		session.proxyInstalled = true;
		session.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kOff)] = true;
		session.admittedFg[static_cast<std::size_t>(
			FrameGenerationMethod::kDLSSG)] = true;
		session.activeFg = FrameGenerationMethod::kDLSSG;
		Check(topology.Admit(session), "activity topology admits");
		topology.SubmitLive(false, SuperResolutionMethod::kNone, 1, true,
			FrameGenerationMethod::kOff, 1);
		const auto& effective = topology.Effective();
		Check(!topology.Pending().required &&
				  IsFrameGenerationActive(
					  effective.frameGeneration != FrameGenerationMethod::kOff,
					  effective.frameGenerationEnabled, true, 20, 200, frame),
			"a pending Off selection does not hide work by the effective FG "
			"provider");
		Check(topology.CommitPendingTransition(
				  1, FrameGenerationMethod::kOff) &&
				  topology.Effective().frameGeneration ==
					  FrameGenerationMethod::kDLSSG &&
				  !topology.Effective().frameGenerationEnabled &&
				  topology.Session()->activeFg ==
					  FrameGenerationMethod::kOff &&
				  !topology.Pending().required,
			"explicit Off disables host FG work and commits the plain private "
			"chain at the frame boundary");

		TopologyState optionsTopology;
		RequestedTopology optionRequest;
		optionRequest.frameGenerationEligible = true;
		optionRequest.frameGenerationEnabled = true;
		optionRequest.frameGeneration =
			FrameGenerationMethod::kDLSSG;
		optionRequest.frameGenerationConfiguration = {
			.mode = FrameGenerationMode::kFixed,
			.fixedMultiplier = 2
		};
		Check(optionsTopology.Freeze(optionRequest),
			"DLSS-G option topology freezes");
		Check(optionsTopology.Admit(session),
			"DLSS-G option topology admits");
		optionsTopology.SubmitLive(false,
			SuperResolutionMethod::kNone, 1, true,
			FrameGenerationMethod::kDLSSG, 2,
			{ .mode = FrameGenerationMode::kDynamic,
				.fixedMultiplier = 2,
				.dynamicTargetFrameRate = 144.0f });
		Check(optionsTopology.PendingTransition().has_value() &&
				optionsTopology.Effective()
						.frameGenerationConfiguration.mode ==
					FrameGenerationMode::kFixed,
			"DLSS-G tuning waits for the existing frame-boundary transaction");
		Check(optionsTopology.CommitPendingTransition(
				  2, FrameGenerationMethod::kDLSSG) &&
				optionsTopology.Effective()
						.frameGenerationConfiguration.mode ==
					FrameGenerationMode::kDynamic &&
				optionsTopology.Effective()
						.frameGenerationConfiguration
						.dynamicTargetFrameRate == 144.0f,
			"a supported DLSS-G tuning change commits without changing the active provider");

		FrameTransaction disabled;
		Check(Capture(disabled, 30) && disabled.PreparePresent(),
			"disabled frame prepares real-frame presentation");
		Check(!IsFrameGenerationActive(true, true, true, 30, 0, disabled),
			"a present prepared without FG work is not active");

		FrameTransaction failed;
		Check(Capture(failed, 31) && failed.PreparePresent(),
			"failed frame reaches presentation");
		failed.SetFrameGenerationPrepared(true);
		Check(!failed.PresentAttempt(false, false, false),
			"failed Present marks the frame failed");
		Check(!IsFrameGenerationActive(true, true, true, 31, 0, failed),
			"failed prepared FG work is not active");
	}

	void TestFrameTransactionLifecycle()
	{
		using namespace cs::render::temporal;

		FrameTransaction invalid;
		Check(!invalid.Capture({ .realFrame = 1 }, { 2560, 1440 }, { 1920, 1080 }) &&
				  invalid.Phase() == FramePhase::kFailed,
			"invalid packet extents fail the transaction");

		FrameTransaction frame;
		Check(Capture(frame, 8, 80, 1), "complete frame packet is captured");
		Check(!Capture(frame, 9), "an in-flight slot cannot be overwritten");
		Check(Capture(frame, 9, 90, 1),
			"a failed slot can be reused by a later packet");
		Check(frame.Failure().empty() && frame.PreparePresent(),
			"slot reuse clears failure and prepares presentation");
		frame.SetFrameGenerationPrepared(true);
		Check(frame.PresentAttempt(true, true, false),
			"DXGI_PRESENT_TEST is observed");
		Check(frame.Phase() == FramePhase::kPresentPrepared &&
				  frame.PresentAttempts() == 0,
			"TEST Present does not consume or count the packet");
		Check(frame.PresentAttempt(false, false, true) &&
				  frame.Phase() == FramePhase::kPresentPrepared &&
				  frame.PresentAttempts() == 1,
			"retryable Present retains the prepared packet");
		Check(frame.PresentAttempt(false, true, false) &&
				  frame.PresentAttempts() == 2 && frame.Retire() &&
				  frame.Phase() == FramePhase::kRetired,
			"accepted retry retires the packet exactly once");

		FrameTransaction slots[2];
		Check(Capture(slots[0], 10) && Capture(slots[1], 11, 0, 1) &&
				  slots[1].Identity().slot == 1,
			"retained slots have independent packet identity");
		slots[1].Abandon();
		Check(slots[1].Phase() == FramePhase::kIdle && Capture(slots[1], 12, 0, 1),
			"interrupted capture abandons ownership before reuse");
	}

	void TestIndependentResetEpochs()
	{
		using namespace cs::render::temporal;

		ResetEpochs epochs;
		Check(epochs.SuperResolutionPending(), "SR reset starts pending");
		Check(epochs.FrameGenerationPending(), "FG reset starts pending");
		Check(epochs.ArmFrameGeneration(), "FG reset arms once");
		Check(!epochs.ArmFrameGeneration(),
			"FG reset is not re-issued before completion");
		Check(epochs.FrameGenerationRequested() == 1,
			"initial FG reset epoch is reported");
		Check(epochs.FrameGenerationConsumed() == 0,
			"initial FG reset is unconsumed");
		epochs.ConsumeSuperResolution(false);
		Check(epochs.SuperResolutionPending(),
			"failed SR work does not consume reset");
		epochs.ConsumeSuperResolution(true);
		Check(!epochs.SuperResolutionPending(), "successful SR work consumes reset");
		Check(epochs.FrameGenerationPending(),
			"SR consumption does not consume FG reset");
		epochs.RequestFrameGeneration();
		Check(epochs.ArmFrameGeneration(), "new FG epoch can be armed");
		epochs.ConsumeFrameGeneration(true);
		Check(!epochs.FrameGenerationPending(), "FG reset consumes independently");
		Check(epochs.FrameGenerationConsumed() == epochs.FrameGenerationRequested(),
			"FG reset epoch reports successful consumption");
	}

	void TestLatencyTimeline()
	{
		using namespace cs::render::temporal;

		LatencyTimeline timeline;
		Check(timeline.Frame() == 0, "latency timeline starts without a frame");
		Check(timeline.BeginFrame() == 1, "normal loop begins the first real frame");
		Check(timeline.Phase() == LatencyPhase::kSleep,
			"sleep precedes message-loop input and simulation");
		Check(timeline.BeginSimulation(), "simulation starts after the message loop");
		Check(timeline.EndSimulationAndBeginRenderSubmit(),
			"main-thread simulation end precedes render submission");
		Check(timeline.BeginPresent(false),
			"first real Present closes render submission");
		Check(timeline.RenderSubmitEnded(),
			"render submission ends exactly before the first Present attempt");
		Check(timeline.EndPresent(false, true),
			"retryable Present completes its attempt");
		Check(timeline.Phase() == LatencyPhase::kPresentRetry,
			"retryable Present retains the real-frame timeline");
		Check(timeline.BeginPresent(false), "same-frame Present retry is accepted");
		Check(timeline.EndPresent(false, false),
			"accepted Present completes the frame");
		Check(timeline.PresentAttempts() == 2,
			"Present retries do not create another real frame");
		Check(timeline.BeginPresent(true), "DXGI_PRESENT_TEST is observational");
		Check(timeline.EndPresent(true, false),
			"DXGI_PRESENT_TEST has no phase effect");
		Check(timeline.BeginFrame() == 2, "next normal loop advances once");
	}

	void TestTypedProviderContracts()
	{
		using namespace cs::render::temporal;

		ColorContract observed{ .resourceFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
			.range = ColorRange::kFull,
			.transfer = TransferFunction::kGamma22,
			.primaries = ColorPrimaries::kUnspecified,
			.stage = ColorStage::kPostTonemapLut,
			.alpha = AlphaMode::kIgnored,
			.exposure = ExposureMode::kAutomatic };
		Check(IsFo4PostTonemapSdr(observed),
			"observed gamma-2.2 post-LUT contract is accepted");
		observed.transfer = TransferFunction::kLinear;
		Check(!IsFo4PostTonemapSdr(observed),
			"linear intermediates cannot masquerade as engine gamma output");

		const SuperResolutionSizeRequest nativeRequest{
			.outputWidth = 3840,
			.outputHeight = 2160,
			.qualityMode = 0
		};
		const SuperResolutionSizeResult nativeSize{
			.result = { .code = ProviderResultCode::kSuccess },
			.renderWidth = nativeRequest.outputWidth,
			.renderHeight = nativeRequest.outputHeight
		};
		SuperResolutionSizeCache sizeCache;
		sizeCache.Store(nativeRequest, nativeSize);
		Check(sizeCache.Find(nativeRequest) &&
				  sizeCache.Find(nativeRequest)->renderWidth ==
					  nativeRequest.outputWidth,
			"provider sizing cache returns only the exact successful request");
		Check(!sizeCache.Find({ .outputWidth = 3840,
				  .outputHeight = 2160,
				  .qualityMode = 1 }),
			"quality changes miss the provider sizing cache");
		sizeCache.Store(nativeRequest,
			{ .result = { .code = ProviderResultCode::kFailure } });
		Check(sizeCache.Find(nativeRequest),
			"failed queries never replace the last usable provider sizing");

		RenderSizeState sizing;
		sizing.SetNative(3441, 1441);
		const SuperResolutionSizeRequest oddRequest{
			.outputWidth = 3441,
			.outputHeight = 1441,
			.qualityMode = 1
		};
		const SuperResolutionSizeResult oddResult{
			.result = { .code = ProviderResultCode::kSuccess },
			.renderWidth = 2024,
			.renderHeight = 848
		};
		Check(sizing.SetRequested(oddRequest, oddResult),
			"valid queried extents are staged");
		Check(sizing.Committed() == RenderExtent{ 3441, 1441 },
			"queried extents remain uncommitted before preflight");
		sizing.CommitRequested();
		Check(sizing.Committed() == RenderExtent{ 2024, 848 },
			"preflight commit preserves exact queried integers");
		Check(static_cast<std::uint32_t>(static_cast<float>(oddRequest.outputWidth) *
										 sizing.WidthRatio()) ==
					  oddResult.renderWidth &&
				  static_cast<std::uint32_t>(
					  static_cast<float>(oddRequest.outputHeight) *
					  sizing.HeightRatio()) == oddResult.renderHeight,
			"published per-axis ratios reconstruct exact queried extents");
		const SuperResolutionSizeResult invalidResult{
			.result = { .code = ProviderResultCode::kFailure,
				.message = "query failed" }
		};
		Check(!sizing.SetRequested(oddRequest, invalidResult) &&
				  sizing.Requested() == RenderExtent{ 3441, 1441 } &&
				  sizing.Committed() == RenderExtent{ 3441, 1441 },
			"failed sizing restores native state before commitment");

		ProviderResult recorded{ .code = ProviderResultCode::kSuccess,
			.workState = ProviderWorkState::kRecorded,
			.outputDependencyEstablished = true };
		Check(!recorded.CanPublishOutput(),
			"recording success alone cannot authorize output publication");
		recorded.workState = ProviderWorkState::kOutputReady;
		Check(recorded.CanPublishOutput(),
			"output publication requires completed work and an established GPU "
			"dependency");
		recorded.outputDependencyEstablished = false;
		Check(!recorded.CanPublishOutput(),
			"missing synchronization dependency denies publication");

	}
}  // namespace

int main()
{
	TestRequestedEffectiveAndPending();
	TestInvalidProviderValues();
	TestQuarantinedConfiguration();
	TestTemporalFeatureLevels();
	TestTemporalAvailabilityPresentation();
	TestLiveTransitions();
	TestSelectedStartupInitialization();
	TestPreUiHandoffPlanning();
	TestFrameGenerationActivity();
	TestFrameTransactionLifecycle();
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
