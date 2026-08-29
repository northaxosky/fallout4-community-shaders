#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace cs::host
{
	enum class IntegrationState : std::uint8_t
	{
		kUndecided,
		kStandalone,
		kRegisteredWaiting,
		kHostedReady,
		kHostedUnavailable
	};

	std::string_view DescribeIntegrationState(IntegrationState a_state) noexcept;

	class IntegrationStateMachine
	{
	public:
		IntegrationState Get() const noexcept { return _state.load(std::memory_order_acquire); }

		bool ChooseStandalone() noexcept;
		bool ChooseStandaloneFromRegistered() noexcept;
		bool ChooseRegistered() noexcept;
		bool MarkReady() noexcept;
		bool MarkUnavailable() noexcept;

		bool IsHosted() const noexcept;
		bool IsReady() const noexcept { return Get() == IntegrationState::kHostedReady; }

	private:
		bool Transition(IntegrationState a_from, IntegrationState a_to) noexcept;

		std::atomic<IntegrationState> _state{ IntegrationState::kUndecided };
	};

	enum class BootstrapAction : std::uint8_t
	{
		kStandaloneNow,
		kDeferForHost
	};

	BootstrapAction DecideBootstrap(IntegrationState a_state) noexcept;

	enum class FallbackAction : std::uint8_t
	{
		kNone,
		kStandaloneFromSavedResources
	};

	FallbackAction DecideFallback(IntegrationState a_state, bool a_bootstrapSeen) noexcept;

	class FallbackCoordination
	{
	public:
		BootstrapAction ObserveBootstrap(IntegrationState a_state) noexcept;
		void MarkResourcesSaved() noexcept { _resourcesSaved = true; }
		FallbackAction OnStandaloneTransition() noexcept;
		bool ConsumeSavedResources() noexcept;

		bool BootstrapSeen() const noexcept { return _bootstrapSeen; }
		bool ResourcesSaved() const noexcept { return _resourcesSaved; }
		bool StandaloneStarted() const noexcept { return _standaloneStarted; }

	private:
		bool _bootstrapSeen{ false };
		bool _resourcesSaved{ false };
		bool _standaloneStarted{ false };
	};
}
