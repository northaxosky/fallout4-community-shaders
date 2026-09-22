#include "Render/TemporalPipelineState.h"
#include "Render/TemporalProvider.h"
#include "Render/TemporalStartup.h"

#include <iostream>
#include <string_view>
#include <vector>

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
		request.superResolution = SuperResolutionMethod::kDLSS;
		const auto selected =
			InitializeSelectedSuperResolution(request, available);
		Check(
			selected.methods[static_cast<std::size_t>(
				SuperResolutionMethod::kDLSS)] &&
				selected.detail.empty() &&
				calls == std::vector{ SuperResolutionMethod::kDLSS },
			"startup initializes only the selected external provider");

		request.upscalingEligible = false;
		calls.clear();
		const auto inactive = InitializeSelectedSuperResolution(request, available);
		Check(calls.empty() &&
				  std::ranges::none_of(inactive.methods,
					  [](bool a_value) { return a_value; }),
			"an unloaded Upscaling feature initializes no SR providers");

		request.upscalingEligible = true;
		request.superResolution = SuperResolutionMethod::kFSR4;
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
			calls == std::vector{ SuperResolutionMethod::kFSR4 } &&
				admission.methods[static_cast<std::size_t>(
					SuperResolutionMethod::kTAA)] &&
				admission.detail.contains("Native TAA"),
			"an unavailable external startup method falls back to native TAA");
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
			"unavailable external startup resolves to native TAA");
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

}  // namespace

int main()
{
	TestRequestedEffectiveAndPending();
	TestQuarantinedConfiguration();
	TestLiveTransitions();
	TestSelectedStartupInitialization();
	TestFrameTransactionLifecycle();
	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Temporal pipeline state checks passed\n";
	return 0;
}
