#include "World/CelestialTransition.h"

#include "Log.h"

#include "RE/B/BGSActorCellEvent.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESLoadGameEvent.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESSleepStopEvent.h"
#include "RE/T/TESWaitStopEvent.h"

#include <atomic>
#include <mutex>

namespace cs::engine
{
	namespace
	{
		auto* L = log::Get("cs.weather");

		std::atomic_bool handlerAvailable{ false };
		std::atomic_bool timeJumpRequested{ false };
		std::atomic_bool gameLoadRequested{ false };
		std::atomic_uint32_t completedGeneration{ 0 };

		void MarkComplete()
		{
			completedGeneration.fetch_add(1, std::memory_order_release);
		}

		void Request(std::atomic_bool& a_request)
		{
			a_request.store(true, std::memory_order_release);
			if (!handlerAvailable.load(std::memory_order_acquire) &&
				a_request.exchange(false, std::memory_order_acq_rel)) {
				MarkComplete();
			}
		}

		template <class Event>
		class TransitionSink final : public RE::BSTEventSink<Event>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const Event& a_event, RE::BSTEventSource<Event>*) override
			{
				if constexpr (std::is_same_v<Event, RE::TESLoadGameEvent>) {
					RequestGameLoadTransition();
				} else {
					if constexpr (std::is_same_v<Event, RE::BGSActorCellEvent>) {
						if (!a_event.flags.any(RE::BGSActorCellEvent::CellFlag::kLeave))
							return RE::BSEventNotifyControl::kContinue;
						const auto* cell = RE::TESForm::GetFormByID<RE::TESObjectCELL>(a_event.cellID);
						if (!cell || cell->IsExterior())
							return RE::BSEventNotifyControl::kContinue;
					}
					RequestTimeJumpTransition();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		template <class Event>
		void RegisterSource(RE::BSTEventSource<Event>* a_source)
		{
			static TransitionSink<Event> sink;
			if (a_source)
				a_source->RegisterSink(&sink);
			else
				L->error("Celestial transition event source is unavailable.");
		}
	}

	void SetCelestialTransitionHandlerAvailable(bool a_available)
	{
		handlerAvailable.store(a_available, std::memory_order_release);
		if (a_available)
			return;

		// Bitwise or, not ||: drain both requests.
		const bool hadPending = timeJumpRequested.exchange(false, std::memory_order_acq_rel) |
		                        gameLoadRequested.exchange(false, std::memory_order_acq_rel);
		if (hadPending)
			MarkComplete();
	}

	void RequestTimeJumpTransition()
	{
		Request(timeJumpRequested);
	}

	void RequestGameLoadTransition()
	{
		Request(gameLoadRequested);
	}

	CelestialTransitionRequest ConsumeCelestialTransitionRequest()
	{
		if (!handlerAvailable.load(std::memory_order_acquire))
			return {};

		return {
			.timeJump = timeJumpRequested.exchange(false, std::memory_order_acq_rel),
			.gameLoad = gameLoadRequested.exchange(false, std::memory_order_acq_rel),
		};
	}

	void CompleteCelestialTransition()
	{
		MarkComplete();
	}

	std::uint32_t GetCompletedCelestialTransitionGeneration()
	{
		return completedGeneration.load(std::memory_order_acquire);
	}

	void RegisterCelestialTransitionSources()
	{
		static std::once_flag once;
		std::call_once(once, [] {
			// FO4: native events stand in for upstream's time-jump sources.
			RegisterSource(RE::TESWaitStopEvent::GetEventSource());
			RegisterSource(RE::TESSleepStopEvent::GetEventSource());
			RegisterSource(RE::TESLoadGameEvent::GetEventSource());
			RegisterSource(static_cast<RE::BSTEventSource<RE::BGSActorCellEvent>*>(
				RE::PlayerCharacter::GetSingleton()));
		});
	}
}
