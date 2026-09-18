#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Render/TemporalProvider.h"

namespace cs::render::temporal
{
	enum class SuperResolutionMethod : std::uint8_t
	{
		kNone,
		kTAA,
		kFSR3,
		kDLSS,
		kFSR4,
		kCount
	};

	enum class FrameGenerationMethod : std::uint8_t
	{
		kOff,
		kFSR3,
		kDLSSG,
		kFSR4,
		kCount
	};

	inline constexpr std::uint32_t kMaxFrameGenerationMethodValue =
		static_cast<std::uint32_t>(FrameGenerationMethod::kFSR4);

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
				(a_domain == FailureDomain::kStreamline &&
					(a_sr == SuperResolutionMethod::kFSR3 ||
						a_sr == SuperResolutionMethod::kFSR4 ||
						a_sr == SuperResolutionMethod::kDLSS)),
			.frameGeneration =
				a_domain == FailureDomain::kFrameGeneration ||
				a_domain == FailureDomain::kEngine ||
				a_domain == FailureDomain::kTransport ||
				a_domain == FailureDomain::kPresentation ||
				(a_domain == FailureDomain::kStreamline &&
					(a_fg == FrameGenerationMethod::kFSR3 ||
						a_fg == FrameGenerationMethod::kFSR4 ||
						a_fg == FrameGenerationMethod::kDLSSG))
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
		SuperResolutionMethod noDlssFallback = SuperResolutionMethod::kTAA;
		FrameGenerationMethod frameGeneration = FrameGenerationMethod::kOff;
		std::uint32_t qualityMode = 1;
		std::uint32_t streamlineLogLevel = 0;
		bool forceFrameGeneration = false;
		bool allowFrameGenerationInMenus = false;
		FrameGenerationConfiguration frameGenerationConfiguration;
		std::uint64_t revision = 0;

		auto operator<=>(const RequestedTopology&) const = default;
	};

	struct SessionTopology
	{
		bool valid = false;
		bool bridgePresent = false;
		bool proxyInstalled = false;
		bool latencyHooksInstalled = false;
		std::array<bool, static_cast<std::size_t>(SuperResolutionMethod::kCount)> admittedSr{};
		std::array<bool, static_cast<std::size_t>(FrameGenerationMethod::kCount)>
			admittedFg{};
		FrameGenerationMethod activeFg = FrameGenerationMethod::kOff;
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
		FrameGenerationConfiguration frameGenerationConfiguration;
		std::uint64_t revision = 0;

		auto operator<=>(const EffectiveConfiguration&) const = default;
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
			_effective.frameGenerationConfiguration =
				_request->frameGenerationConfiguration;
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
			const auto fgIndex =
				static_cast<std::size_t>(_effective.frameGeneration);
			if (fgIndex >= _session->admittedFg.size() ||
				!_session->admittedFg[fgIndex]) {
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
			std::uint64_t a_revision,
			FrameGenerationConfiguration a_fgConfiguration = {})
		{
			if (!_request) {
				return;
			}

			_request->superResolutionEnabled = a_srEnabled;
			_request->superResolution = a_sr;
			_request->qualityMode = a_qualityMode;
			_request->frameGenerationEnabled = a_fgEnabled;
			_request->frameGeneration = a_fg;
			_request->frameGenerationConfiguration = a_fgConfiguration;
			_request->revision = a_revision;
			if (!_session) {
				_restartForSuperResolutionMethod =
					a_sr != _startupRequest->superResolution;
				_restartForFrameGenerationMethod =
					a_fg != _startupRequest->frameGeneration;
				UpdatePendingRestart();
				return;
			}

			const auto& transitionBase =
				_transitionInFlight ? *_transitionInFlight : _effective;
			auto target = transitionBase;
			const auto srIndex = static_cast<std::size_t>(a_sr);
			const auto fgIndex = static_cast<std::size_t>(a_fg);
			const bool srAdmitted =
				srIndex < _session->admittedSr.size() &&
				_session->admittedSr[srIndex];
			const bool fgAdmitted =
				fgIndex < _session->admittedFg.size() &&
				_session->admittedFg[fgIndex];
			const bool externalSrRequested =
				a_sr == SuperResolutionMethod::kFSR3 ||
				a_sr == SuperResolutionMethod::kFSR4 ||
				a_sr == SuperResolutionMethod::kDLSS;
			_restartForSuperResolutionMethod =
				a_srEnabled && externalSrRequested &&
				a_sr != _startupRequest->superResolution && !srAdmitted;
			_restartForFrameGenerationMethod =
				a_fgEnabled && a_fg != FrameGenerationMethod::kOff &&
				!fgAdmitted;
			if (!_quarantined.superResolution &&
				(!_superResolutionFailed || !externalSrRequested)) {
				if (!a_srEnabled) {
					target.superResolutionEnabled = false;
				} else if (srAdmitted &&
					!_restartForSuperResolutionMethod) {
					target.superResolution = a_sr;
					target.qualityMode = a_qualityMode;
					target.superResolutionEnabled =
						_startupRequest->upscalingEligible &&
						a_sr != SuperResolutionMethod::kNone;
				}
			}
			if (!_quarantined.frameGeneration &&
				!_frameGenerationFailed) {
				if (!a_fgEnabled ||
					a_fg == FrameGenerationMethod::kOff) {
					target.frameGenerationEnabled = false;
					if (a_fg != FrameGenerationMethod::kOff &&
						fgAdmitted) {
						target.frameGeneration = a_fg;
					}
				} else if (fgAdmitted &&
					!_restartForFrameGenerationMethod) {
					target.frameGeneration = a_fg;
					if (a_fg == FrameGenerationMethod::kDLSSG) {
						target.frameGenerationConfiguration =
							a_fgConfiguration;
					}
					target.frameGenerationEnabled =
						_startupRequest->frameGenerationEligible &&
						a_fg != FrameGenerationMethod::kOff &&
						_session->proxyInstalled;
				}
			}
			const bool changed =
				target.superResolutionEnabled !=
					transitionBase.superResolutionEnabled ||
				target.frameGenerationEnabled !=
					transitionBase.frameGenerationEnabled ||
				target.superResolution != transitionBase.superResolution ||
				target.frameGeneration != transitionBase.frameGeneration ||
				target.frameGenerationConfiguration !=
					transitionBase.frameGenerationConfiguration ||
				target.qualityMode != transitionBase.qualityMode;
			if (!changed) {
				_pendingTransition.reset();
			} else {
				target.revision = a_revision;
				_pendingTransition = target;
			}
			UpdatePendingRestart();
		}

		[[nodiscard]] const std::optional<EffectiveConfiguration>&
		PendingTransition() const noexcept
		{
			return _pendingTransition;
		}

		[[nodiscard]] std::optional<EffectiveConfiguration>
		BeginPendingTransition() noexcept
		{
			if (_transitionInFlight || !_pendingTransition) {
				return std::nullopt;
			}
			_transitionInFlight = _pendingTransition;
			_pendingTransition.reset();
			return _transitionInFlight;
		}

		void DeferTransition(std::uint64_t a_revision) noexcept
		{
			if (!_transitionInFlight ||
				_transitionInFlight->revision != a_revision) {
				return;
			}
			if (!_pendingTransition ||
				_pendingTransition->revision < a_revision) {
				_pendingTransition = _transitionInFlight;
			}
			_transitionInFlight.reset();
		}

		[[nodiscard]] bool CommitPendingTransition(
			std::uint64_t a_revision) noexcept
		{
			return CommitPendingTransition(
				a_revision,
				_pendingTransition &&
						_pendingTransition->frameGenerationEnabled
					? _pendingTransition->frameGeneration
					: FrameGenerationMethod::kOff);
		}

		[[nodiscard]] bool CommitPendingTransition(
			std::uint64_t a_revision,
			FrameGenerationMethod a_activeFrameGeneration) noexcept
		{
			const EffectiveConfiguration* completed = nullptr;
			if (_transitionInFlight &&
				_transitionInFlight->revision == a_revision) {
				completed = &*_transitionInFlight;
			} else if (_pendingTransition &&
				_pendingTransition->revision == a_revision) {
				completed = &*_pendingTransition;
			}
			if (!completed) {
				return false;
			}
			if (_session) {
				_session->activeFg = a_activeFrameGeneration;
			}
			_effective = *completed;
			if (_transitionInFlight &&
				_transitionInFlight->revision == a_revision) {
				_transitionInFlight.reset();
			} else {
				_pendingTransition.reset();
			}
			_frameGenerationFailed = false;
			_frameGenerationFailureReason.clear();
			UpdateEffectiveEnablement();
			UpdatePendingRestart();
			return true;
		}

		[[nodiscard]] bool
		CommitPendingFrameGenerationFallback(
			std::uint64_t a_revision,
			std::string a_reason) noexcept
		{
			const EffectiveConfiguration* completed = nullptr;
			if (_transitionInFlight &&
				_transitionInFlight->revision == a_revision) {
				completed = &*_transitionInFlight;
			} else if (_pendingTransition &&
				_pendingTransition->revision == a_revision) {
				completed = &*_pendingTransition;
			}
			if (!completed) {
				return false;
			}
			_effective = *completed;
			_effective.frameGenerationEnabled = false;
			_effective.frameGeneration = FrameGenerationMethod::kOff;
			if (_session) {
				_session->activeFg = FrameGenerationMethod::kOff;
			}
			if (_transitionInFlight &&
				_transitionInFlight->revision == a_revision) {
				_transitionInFlight.reset();
			} else {
				_pendingTransition.reset();
			}
			if (_pendingTransition &&
				_pendingTransition->frameGenerationEnabled) {
				_pendingTransition.reset();
			}
			_frameGenerationFailed = true;
			_frameGenerationFailureReason = std::move(a_reason);
			UpdateEffectiveEnablement();
			UpdatePendingRestart();
			return true;
		}

		void RejectPendingTransition(std::uint64_t a_revision) noexcept
		{
			if (_transitionInFlight &&
				_transitionInFlight->revision == a_revision) {
				_transitionInFlight.reset();
			} else if (_pendingTransition &&
				_pendingTransition->revision == a_revision) {
				_pendingTransition.reset();
			}
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
				if (_session) {
					_session->activeFg = FrameGenerationMethod::kOff;
				}
			}
			_pendingTransition.reset();
			_transitionInFlight.reset();
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
			_pendingTransition.reset();
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
					static_cast<std::size_t>(_effective.frameGeneration) <
						_session->admittedFg.size() &&
					_session->admittedFg[static_cast<std::size_t>(
						_effective.frameGeneration)] &&
					(_effective.frameGeneration ==
							FrameGenerationMethod::kOff ||
						_session->activeFg ==
							_effective.frameGeneration));
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
			} else if (_frameGenerationFailed) {
				_pending.required = true;
				_pending.reason = _frameGenerationFailureReason;
			} else if (_restartForSuperResolutionMethod ||
				_restartForFrameGenerationMethod) {
				_pending.required = true;
				_pending.reason =
					_restartForSuperResolutionMethod
						? (_session
								  ? "The requested super-resolution provider is "
									"unavailable in the current session."
								  : "The requested super-resolution method differs "
									"from the frozen startup request.")
						: "The requested frame-generation provider is unavailable "
						  "in the current session.";
			}
		}

		std::optional<RequestedTopology> _request;
		std::optional<RequestedTopology> _startupRequest;
		std::optional<SessionTopology> _session;
		std::optional<EffectiveConfiguration> _pendingTransition;
		std::optional<EffectiveConfiguration> _transitionInFlight;
		EffectiveConfiguration _effective;
		PendingRestart _pending;
		bool _superResolutionFailed = false;
		std::string _superResolutionFailureReason;
		bool _frameGenerationFailed = false;
		std::string _frameGenerationFailureReason;
		bool _restartForSuperResolutionMethod = false;
		bool _restartForFrameGenerationMethod = false;
		FailureImpact _quarantined;
		std::string _quarantineReason;
	};

	enum class FramePhase : std::uint8_t
	{
		kIdle,
		kCaptured,
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

	struct FrameIdentity
	{
		std::uint64_t realFrame = 0;
		std::uint64_t engineFrame = 0;
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
		[[nodiscard]] bool Capture(
			FrameIdentity a_identity,
			Extent a_renderExtent,
			Extent a_outputExtent) noexcept
		{
			if (_phase != FramePhase::kIdle && _phase != FramePhase::kRetired &&
				_phase != FramePhase::kFailed) {
				return Fail("frame slot was reused before retirement");
			}
			if (!a_renderExtent.IsValid() || !a_outputExtent.IsValid() ||
				a_renderExtent.width > a_outputExtent.width ||
				a_renderExtent.height > a_outputExtent.height) {
				return Fail("invalid frame packet extents");
			}
			*this = FrameTransaction{};
			_identity = a_identity;
			_phase = FramePhase::kCaptured;
			return true;
		}

		[[nodiscard]] bool PreparePresent() noexcept
		{
			if (_phase != FramePhase::kCaptured) {
				return Fail("Present prepared without a captured frame packet");
			}
			_phase = FramePhase::kPresentPrepared;
			return true;
		}

		void SetFrameGenerationPrepared(bool a_prepared) noexcept
		{
			if (_phase == FramePhase::kPresentPrepared) {
				_frameGenerationPrepared = a_prepared;
			}
		}

		void Abandon() noexcept
		{
			if (_phase == FramePhase::kCaptured ||
				_phase == FramePhase::kPresentPrepared) {
				*this = FrameTransaction{};
			}
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
		FramePhase _phase = FramePhase::kIdle;
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
