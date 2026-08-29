#include "Host/IntegrationState.h"

namespace cs::host
{
	std::string_view DescribeIntegrationState(IntegrationState a_state) noexcept
	{
		switch (a_state) {
		case IntegrationState::kUndecided:
			return "undecided";
		case IntegrationState::kStandalone:
			return "standalone";
		case IntegrationState::kRegisteredWaiting:
			return "registered, waiting for the host";
		case IntegrationState::kHostedReady:
			return "hosted";
		case IntegrationState::kHostedUnavailable:
			return "host unavailable, falling back";
		default:
			return "unknown";
		}
	}

	bool IntegrationStateMachine::Transition(IntegrationState a_from, IntegrationState a_to) noexcept
	{
		auto expected = a_from;
		return _state.compare_exchange_strong(
			expected,
			a_to,
			std::memory_order_acq_rel,
			std::memory_order_acquire);
	}

	bool IntegrationStateMachine::ChooseStandalone() noexcept
	{
		return Transition(IntegrationState::kUndecided, IntegrationState::kStandalone);
	}

	bool IntegrationStateMachine::ChooseStandaloneFromRegistered() noexcept
	{
		return Transition(IntegrationState::kRegisteredWaiting, IntegrationState::kStandalone);
	}

	bool IntegrationStateMachine::ChooseRegistered() noexcept
	{
		return Transition(IntegrationState::kUndecided, IntegrationState::kRegisteredWaiting);
	}

	bool IntegrationStateMachine::MarkReady() noexcept
	{
		return Transition(IntegrationState::kRegisteredWaiting, IntegrationState::kHostedReady);
	}

	bool IntegrationStateMachine::MarkUnavailable() noexcept
	{
		return Transition(IntegrationState::kRegisteredWaiting, IntegrationState::kHostedUnavailable);
	}

	bool IntegrationStateMachine::IsHosted() const noexcept
	{
		const auto state = Get();
		return state == IntegrationState::kRegisteredWaiting || state == IntegrationState::kHostedReady;
	}

	BootstrapAction DecideBootstrap(IntegrationState a_state) noexcept
	{
		switch (a_state) {
		case IntegrationState::kRegisteredWaiting:
		case IntegrationState::kHostedReady:
			return BootstrapAction::kDeferForHost;
		default:
			return BootstrapAction::kStandaloneNow;
		}
	}

	FallbackAction DecideFallback(IntegrationState a_state, bool a_bootstrapSeen) noexcept
	{
		if (a_state != IntegrationState::kHostedUnavailable)
			return FallbackAction::kNone;
		return a_bootstrapSeen ? FallbackAction::kStandaloneFromSavedResources : FallbackAction::kNone;
	}
}
