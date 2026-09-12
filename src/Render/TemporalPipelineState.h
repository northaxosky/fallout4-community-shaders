#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cs::render::temporal
{
	enum class SuperResolutionMethod : std::uint8_t
	{
		kNone,
		kTAA,
		kFSR3,
		kDLSS,
		kCount
	};

	enum class FrameGenerationMethod : std::uint8_t
	{
		kOff,
		kFSR3,
		kDLSSG,
		kCount
	};

	inline constexpr std::uint32_t kMaxFrameGenerationMethodValue =
		static_cast<std::uint32_t>(FrameGenerationMethod::kDLSSG);

	enum class GraphicsApi : std::uint8_t
	{
		kD3D11,
		kD3D12
	};

	enum class FailureDomain : std::uint8_t
	{
		kNone,
		kConfiguration,
		kEngine,
		kSuperResolution,
		kFrameGeneration,
		kStreamline,
		kTransport,
		kPresentation
	};

	struct FailureImpact
	{
		bool superResolution = false;
		bool frameGeneration = false;
	};

	[[nodiscard]] constexpr FailureImpact ClassifyFailure(
		FailureDomain a_domain,
		SuperResolutionMethod a_sr,
		FrameGenerationMethod a_fg) noexcept
	{
		return {
			.superResolution =
				a_domain == FailureDomain::kSuperResolution ||
				a_domain == FailureDomain::kEngine ||
				a_domain == FailureDomain::kTransport ||
				(a_domain == FailureDomain::kStreamline && a_sr == SuperResolutionMethod::kDLSS),
			.frameGeneration =
				a_domain == FailureDomain::kFrameGeneration ||
				a_domain == FailureDomain::kEngine ||
				a_domain == FailureDomain::kTransport ||
				a_domain == FailureDomain::kPresentation ||
				(a_domain == FailureDomain::kStreamline && a_fg == FrameGenerationMethod::kDLSSG)
		};
	}

	struct Extent
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		[[nodiscard]] bool IsValid() const noexcept { return width != 0 && height != 0; }
		auto operator<=>(const Extent&) const = default;
	};

	struct RequestedTopology
	{
		bool upscalingEligible = false;
		bool frameGenerationEligible = false;
		bool superResolutionEnabled = true;
		bool frameGenerationEnabled = true;
		SuperResolutionMethod superResolution = SuperResolutionMethod::kDLSS;
		SuperResolutionMethod noDlssFallback = SuperResolutionMethod::kFSR3;
		FrameGenerationMethod frameGeneration = FrameGenerationMethod::kOff;
		std::uint32_t qualityMode = 1;
		std::uint32_t streamlineLogLevel = 0;
		bool forceFrameGeneration = false;
		bool allowFrameGenerationInMenus = false;
		std::uint64_t revision = 0;

		auto operator<=>(const RequestedTopology&) const = default;
	};

	struct SessionTopology
	{
		bool valid = false;
		GraphicsApi streamlineApi = GraphicsApi::kD3D11;
		bool bridgePresent = false;
		bool proxyInstalled = false;
		bool latencyHooksInstalled = false;
		std::array<bool, static_cast<std::size_t>(SuperResolutionMethod::kCount)> admittedSr{};
		FrameGenerationMethod admittedFg = FrameGenerationMethod::kOff;
		std::uint64_t adapterLuid = 0;
		std::string rejectionReason;
	};

	struct DisplayState
	{
		Extent output;
		Extent viewport;
		std::uint32_t format = 0;
		std::uint64_t deviceGeneration = 0;
		std::uint64_t displayGeneration = 0;
		std::uint64_t engineResourceGeneration = 0;
	};

	struct EffectiveConfiguration
	{
		bool superResolutionEnabled = false;
		bool frameGenerationEnabled = false;
		SuperResolutionMethod superResolution = SuperResolutionMethod::kNone;
		FrameGenerationMethod frameGeneration = FrameGenerationMethod::kOff;
		std::uint32_t qualityMode = 1;
		std::uint64_t revision = 0;
	};

	struct PendingRestart
	{
		bool required = false;
		SuperResolutionMethod requestedSr = SuperResolutionMethod::kNone;
		FrameGenerationMethod requestedFg = FrameGenerationMethod::kOff;
		std::string reason;
	};

	class TopologyState
	{
	public:
		[[nodiscard]] bool Freeze(RequestedTopology a_request) noexcept
		{
			if (_request) {
				return false;
			}
			_startupRequest = a_request;
			_request = std::move(a_request);
			_effective.superResolution = _request->superResolution;
			_effective.frameGeneration = _request->frameGeneration;
			_effective.qualityMode = _request->qualityMode;
			_effective.revision = _request->revision;
			UpdateEffectiveEnablement();
			return true;
		}

		[[nodiscard]] bool Admit(SessionTopology a_session) noexcept
		{
			if (!_request || _session) {
				return false;
			}
			_session = std::move(a_session);
			if (!_session->valid) {
				_effective.superResolutionEnabled = false;
				_effective.frameGenerationEnabled = false;
				_effective.superResolution = SuperResolutionMethod::kNone;
				_effective.frameGeneration = FrameGenerationMethod::kOff;
				return true;
			}

			const auto srIndex = static_cast<std::size_t>(_effective.superResolution);
			if (srIndex >= _session->admittedSr.size() || !_session->admittedSr[srIndex]) {
				const auto fallbackIndex =
					static_cast<std::size_t>(_startupRequest->noDlssFallback);
				if (_effective.superResolution == SuperResolutionMethod::kDLSS &&
					fallbackIndex < _session->admittedSr.size() &&
					_session->admittedSr[fallbackIndex]) {
					_effective.superResolution = _startupRequest->noDlssFallback;
				} else {
					_effective.superResolutionEnabled = false;
					_effective.superResolution = SuperResolutionMethod::kNone;
				}
			}
			if (_effective.frameGeneration != _session->admittedFg) {
				_effective.frameGenerationEnabled = false;
				_effective.frameGeneration = FrameGenerationMethod::kOff;
			}
			UpdateEffectiveEnablement();
			return true;
		}

		void SubmitLive(
			bool a_srEnabled,
			SuperResolutionMethod a_sr,
			std::uint32_t a_qualityMode,
			bool a_fgEnabled,
			FrameGenerationMethod a_fg,
			std::uint64_t a_revision)
		{
			if (!_request) {
				return;
			}

			_request->superResolutionEnabled = a_srEnabled;
			_request->superResolution = a_sr;
			_request->qualityMode = a_qualityMode;
			_request->frameGenerationEnabled = a_fgEnabled;
			_request->frameGeneration = a_fg;
			_request->revision = a_revision;
			_effective.revision = a_revision;
			_effective.qualityMode = a_qualityMode;
			UpdateEffectiveEnablement();
			UpdatePendingRestart();
		}

		void Quarantine(
			FailureImpact a_impact,
			std::uint64_t a_revision,
			std::string a_reason)
		{
			if (!a_impact.superResolution && !a_impact.frameGeneration) {
				return;
			}
			_quarantined.superResolution |= a_impact.superResolution;
			_quarantined.frameGeneration |= a_impact.frameGeneration;
			_quarantineReason = std::move(a_reason);
			if (_quarantined.superResolution) {
				_effective.superResolutionEnabled = false;
				_effective.superResolution = SuperResolutionMethod::kNone;
			}
			if (_quarantined.frameGeneration) {
				_effective.frameGenerationEnabled = false;
				_effective.frameGeneration = FrameGenerationMethod::kOff;
			}
			_effective.revision = a_revision;
			UpdatePendingRestart();
		}

		void FailSuperResolutionToNative(
			std::uint64_t a_revision,
			std::string a_reason)
		{
			if (_quarantined.superResolution) {
				UpdatePendingRestart();
				return;
			}
			_superResolutionFailed = true;
			_superResolutionFailureReason = std::move(a_reason);
			_effective.superResolution = SuperResolutionMethod::kTAA;
			_effective.revision = a_revision;
			UpdateEffectiveEnablement();
			UpdatePendingRestart();
		}

		[[nodiscard]] const std::optional<RequestedTopology>& Request() const noexcept { return _request; }
		[[nodiscard]] const std::optional<RequestedTopology>& StartupRequest() const noexcept { return _startupRequest; }
		[[nodiscard]] const std::optional<SessionTopology>& Session() const noexcept { return _session; }
		[[nodiscard]] const EffectiveConfiguration& Effective() const noexcept { return _effective; }
		[[nodiscard]] const PendingRestart& Pending() const noexcept { return _pending; }

	private:
		void UpdateEffectiveEnablement() noexcept
		{
			const auto srIndex = static_cast<std::size_t>(_effective.superResolution);
			const bool srReady = !_session ||
				(_session->valid && srIndex < _session->admittedSr.size() &&
					_session->admittedSr[srIndex]);
			const bool fgReady = !_session ||
				(_session->valid && _session->proxyInstalled &&
					_session->admittedFg == _effective.frameGeneration);
			_effective.superResolutionEnabled =
				_request && _request->superResolutionEnabled && _startupRequest->upscalingEligible &&
				_effective.superResolution != SuperResolutionMethod::kNone &&
				srReady && !_quarantined.superResolution;
			_effective.frameGenerationEnabled =
				_request && _request->frameGenerationEnabled && _startupRequest->frameGenerationEligible &&
				_effective.frameGeneration != FrameGenerationMethod::kOff &&
				fgReady && !_quarantined.frameGeneration;
		}

		void UpdatePendingRestart()
		{
			_pending = {};
			if (!_request || !_startupRequest)
				return;
			_pending.requestedSr = _request->superResolution;
			_pending.requestedFg = _request->frameGeneration;
			if (_quarantined.superResolution || _quarantined.frameGeneration) {
				_pending.required = true;
				_pending.reason = _quarantineReason +
					" Restart required before the affected temporal processing can resume.";
			} else if (_superResolutionFailed) {
				_pending.required = true;
				_pending.reason = _superResolutionFailureReason;
			} else if (_request->superResolution != _startupRequest->superResolution ||
				_request->frameGeneration != _startupRequest->frameGeneration) {
				_pending.required = true;
				_pending.reason = "Super-resolution and frame-generation methods are applied at startup.";
			}
		}

		std::optional<RequestedTopology> _request;
		std::optional<RequestedTopology> _startupRequest;
		std::optional<SessionTopology> _session;
		EffectiveConfiguration _effective;
		PendingRestart _pending;
		bool _superResolutionFailed = false;
		std::string _superResolutionFailureReason;
		FailureImpact _quarantined;
		std::string _quarantineReason;
	};

	enum class FramePhase : std::uint8_t
	{
		kIdle,
		kBegun,
		kPlanned,
		kRenderStateCommitted,
		kWorldCaptured,
		kSceneResolved,
		kPreUiCaptured,
		kFinalCaptured,
		kPresentPrepared,
		kPresentAccepted,
		kRetired,
		kFailed
	};

	enum class LatencyPhase : std::uint8_t
	{
		kIdle,
		kSleep,
		kSimulation,
		kRenderSubmit,
		kPresent,
		kPresentRetry,
		kComplete
	};

	class LatencyTimeline
	{
	public:
		[[nodiscard]] std::uint64_t BeginFrame() noexcept
		{
			++_frame;
			_phase = LatencyPhase::kSleep;
			_presentAttempts = 0;
			_renderSubmitEnded = false;
			return _frame;
		}

		[[nodiscard]] bool BeginSimulation() noexcept
		{
			if (_phase != LatencyPhase::kSleep) {
				return false;
			}
			_phase = LatencyPhase::kSimulation;
			return true;
		}

		[[nodiscard]] bool EndSimulationAndBeginRenderSubmit() noexcept
		{
			if (_phase != LatencyPhase::kSimulation) {
				return false;
			}
			_phase = LatencyPhase::kRenderSubmit;
			return true;
		}

		[[nodiscard]] bool BeginPresent(bool a_testOnly) noexcept
		{
			if (a_testOnly) {
				return true;
			}
			if (_phase != LatencyPhase::kRenderSubmit &&
				_phase != LatencyPhase::kPresentRetry) {
				return false;
			}
			_renderSubmitEnded = true;
			_phase = LatencyPhase::kPresent;
			++_presentAttempts;
			return true;
		}

		[[nodiscard]] bool EndPresent(
			bool a_testOnly,
			bool a_retryable) noexcept
		{
			if (a_testOnly) {
				return true;
			}
			if (_phase != LatencyPhase::kPresent) {
				return false;
			}
			_phase = a_retryable
				? LatencyPhase::kPresentRetry
				: LatencyPhase::kComplete;
			return true;
		}

		[[nodiscard]] std::uint64_t Frame() const noexcept { return _frame; }
		[[nodiscard]] LatencyPhase Phase() const noexcept { return _phase; }
		[[nodiscard]] std::uint32_t PresentAttempts() const noexcept { return _presentAttempts; }
		[[nodiscard]] bool RenderSubmitEnded() const noexcept { return _renderSubmitEnded; }

	private:
		std::uint64_t _frame = 0;
		LatencyPhase _phase = LatencyPhase::kIdle;
		std::uint32_t _presentAttempts = 0;
		bool _renderSubmitEnded = false;
	};

	enum class SceneResolution : std::uint8_t
	{
		kExternalPublished,
		kNativeCompleted,
		kFailed
	};

	struct FrameIdentity
	{
		std::uint64_t realFrame = 0;
		std::uint64_t engineFrame = 0;
		std::uint64_t configurationRevision = 0;
		std::uint64_t deviceGeneration = 0;
		std::uint64_t displayGeneration = 0;
		std::uint64_t engineResourceGeneration = 0;
		std::uint32_t slot = 0;
	};

	struct PreUiHandoffPlan
	{
		bool captureFrameGenerationInputs = false;
		bool driveSuperResolution = false;

		[[nodiscard]] bool ShouldCaptureHudlessColor(
			bool a_superResolutionResolved) const noexcept
		{
			return captureFrameGenerationInputs &&
				(!driveSuperResolution || a_superResolutionResolved);
		}
	};

	[[nodiscard]] constexpr PreUiHandoffPlan PlanPreUiHandoff(
		bool a_fullEffectsPath,
		bool a_frameGenerationEnabled,
		bool a_superResolutionDriving) noexcept
	{
		if (!a_fullEffectsPath) {
			return {};
		}
		return {
			.captureFrameGenerationInputs = a_frameGenerationEnabled,
			.driveSuperResolution = a_superResolutionDriving
		};
	}

	class FrameTransaction
	{
	public:
		[[nodiscard]] bool Begin(FrameIdentity a_identity) noexcept
		{
			if (_phase != FramePhase::kIdle && _phase != FramePhase::kRetired &&
				_phase != FramePhase::kFailed) {
				return Fail("frame slot was reused before retirement");
			}
			*this = FrameTransaction{};
			_identity = a_identity;
			_phase = FramePhase::kBegun;
			return true;
		}

		[[nodiscard]] bool Plan(Extent a_requested, Extent a_output) noexcept
		{
			if (_phase != FramePhase::kBegun || !a_requested.IsValid() || !a_output.IsValid() ||
				a_requested.width > a_output.width || a_requested.height > a_output.height) {
				return Fail("invalid frame plan");
			}
			_requestedExtent = a_requested;
			_outputExtent = a_output;
			_phase = FramePhase::kPlanned;
			return true;
		}

		[[nodiscard]] bool CommitRenderState(Extent a_committed) noexcept
		{
			if (_phase != FramePhase::kPlanned || a_committed != _requestedExtent) {
				return Fail("render extent changed after planning");
			}
			_committedExtent = a_committed;
			_phase = FramePhase::kRenderStateCommitted;
			return true;
		}

		[[nodiscard]] bool CaptureWorld(bool a_valid) noexcept
		{
			if (_phase != FramePhase::kRenderStateCommitted || !a_valid) {
				return Fail("world inputs are unavailable");
			}
			_phase = FramePhase::kWorldCaptured;
			return true;
		}

		[[nodiscard]] bool ResolveScene(SceneResolution a_resolution) noexcept
		{
			if (_phase != FramePhase::kWorldCaptured || _evaluationAttempted) {
				return Fail("scene resolve was attempted more than once");
			}
			_evaluationAttempted = true;
			_sceneResolution = a_resolution;
			if (a_resolution == SceneResolution::kFailed) {
				return Fail("scene resolve failed");
			}
			_published = a_resolution == SceneResolution::kExternalPublished;
			_phase = FramePhase::kSceneResolved;
			return true;
		}

		[[nodiscard]] bool CapturePreUi() noexcept
		{
			if (_phase != FramePhase::kSceneResolved || _preUiCaptured) {
				return Fail("pre-UI capture is out of phase");
			}
			_preUiCaptured = true;
			_phase = FramePhase::kPreUiCaptured;
			return true;
		}

		[[nodiscard]] bool CaptureFinal() noexcept
		{
			if (_phase != FramePhase::kPreUiCaptured || _finalCaptured) {
				return Fail("final-color capture is out of phase");
			}
			_finalCaptured = true;
			_phase = FramePhase::kFinalCaptured;
			return true;
		}

		[[nodiscard]] bool PreparePresent(bool a_prepareFrameGeneration) noexcept
		{
			if (_phase != FramePhase::kFinalCaptured || _presentPrepared) {
				return Fail("presentation preparation is out of phase");
			}
			_presentPrepared = true;
			_frameGenerationPrepared = a_prepareFrameGeneration;
			_phase = FramePhase::kPresentPrepared;
			return true;
		}

		[[nodiscard]] bool PresentAttempt(bool a_testOnly, bool a_accepted, bool a_retryable) noexcept
		{
			if (a_testOnly) {
				return _phase == FramePhase::kPresentPrepared ||
					_phase == FramePhase::kPresentAccepted;
			}
			if (_phase != FramePhase::kPresentPrepared) {
				return Fail("Present was attempted without a prepared frame");
			}
			++_presentAttempts;
			if (a_retryable) {
				return true;
			}
			if (!a_accepted) {
				return Fail("Present failed");
			}
			_phase = FramePhase::kPresentAccepted;
			return true;
		}

		[[nodiscard]] bool Retire() noexcept
		{
			if (_phase != FramePhase::kPresentAccepted) {
				return Fail("frame retired before Present acceptance");
			}
			_phase = FramePhase::kRetired;
			return true;
		}

		[[nodiscard]] FramePhase Phase() const noexcept { return _phase; }
		[[nodiscard]] const FrameIdentity& Identity() const noexcept { return _identity; }
		[[nodiscard]] Extent RequestedExtent() const noexcept { return _requestedExtent; }
		[[nodiscard]] Extent CommittedExtent() const noexcept { return _committedExtent; }
		[[nodiscard]] Extent OutputExtent() const noexcept { return _outputExtent; }
		[[nodiscard]] SceneResolution Resolution() const noexcept { return _sceneResolution; }
		[[nodiscard]] bool Published() const noexcept { return _published; }
		[[nodiscard]] bool FrameGenerationPrepared() const noexcept { return _frameGenerationPrepared; }
		[[nodiscard]] bool HasRecentFrameGenerationWork(
			std::uint64_t a_currentRealFrame,
			std::optional<std::uint64_t> a_currentEngineFrame) const noexcept
		{
			if (!_frameGenerationPrepared ||
				_identity.realFrame > a_currentRealFrame ||
				!a_currentEngineFrame ||
				_identity.engineFrame > *a_currentEngineFrame) {
				return false;
			}
			// Composite telemetry runs before this frame's pre-UI capture and Present.
			if (*a_currentEngineFrame - _identity.engineFrame > 1) {
				return false;
			}
			return _phase == FramePhase::kPresentPrepared ||
				_phase == FramePhase::kPresentAccepted ||
				_phase == FramePhase::kRetired;
		}
		[[nodiscard]] std::uint32_t PresentAttempts() const noexcept { return _presentAttempts; }
		[[nodiscard]] std::string_view Failure() const noexcept { return _failure; }

	private:
		[[nodiscard]] bool Fail(std::string_view a_reason) noexcept
		{
			_phase = FramePhase::kFailed;
			_failure = a_reason;
			return false;
		}

		FrameIdentity _identity;
		Extent _requestedExtent;
		Extent _committedExtent;
		Extent _outputExtent;
		FramePhase _phase = FramePhase::kIdle;
		SceneResolution _sceneResolution = SceneResolution::kFailed;
		bool _evaluationAttempted = false;
		bool _published = false;
		bool _preUiCaptured = false;
		bool _finalCaptured = false;
		bool _presentPrepared = false;
		bool _frameGenerationPrepared = false;
		std::uint32_t _presentAttempts = 0;
		std::string_view _failure;
	};

	[[nodiscard]] inline bool IsFrameGenerationActive(
		bool a_configured,
		bool a_effective,
		bool a_ready,
		std::uint64_t a_currentRealFrame,
		std::optional<std::uint64_t> a_currentEngineFrame,
		const FrameTransaction& a_frame) noexcept
	{
		return a_configured && a_effective && a_ready &&
			a_frame.HasRecentFrameGenerationWork(
				a_currentRealFrame, a_currentEngineFrame);
	}

	class ResetEpochs
	{
	public:
		void RequestSuperResolution() noexcept { ++_srRequested; }
		void RequestFrameGeneration() noexcept { ++_fgRequested; }
		[[nodiscard]] bool SuperResolutionPending() const noexcept { return _srConsumed != _srRequested; }
		[[nodiscard]] bool FrameGenerationPending() const noexcept { return _fgConsumed != _fgRequested; }
		[[nodiscard]] std::uint64_t SuperResolutionRequested() const noexcept { return _srRequested; }
		[[nodiscard]] std::uint64_t SuperResolutionConsumed() const noexcept { return _srConsumed; }
		[[nodiscard]] std::uint64_t FrameGenerationRequested() const noexcept { return _fgRequested; }
		[[nodiscard]] std::uint64_t FrameGenerationConsumed() const noexcept { return _fgConsumed; }
		[[nodiscard]] bool ArmFrameGeneration() noexcept
		{
			if (_fgIssued == _fgRequested) {
				return false;
			}
			_fgIssued = _fgRequested;
			return true;
		}
		void ConsumeSuperResolution(bool a_completed) noexcept
		{
			if (a_completed) {
				_srConsumed = _srRequested;
			}
		}
		void ConsumeFrameGeneration(bool a_completed) noexcept
		{
			if (a_completed) {
				_fgConsumed = _fgRequested;
			}
		}
	private:
		std::uint64_t _srRequested = 1;
		std::uint64_t _srConsumed = 0;
		std::uint64_t _fgRequested = 1;
		std::uint64_t _fgIssued = 0;
		std::uint64_t _fgConsumed = 0;
	};
}
