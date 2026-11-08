#pragma once

#include "Log.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace cs::capture
{
	using RequestId = std::uint64_t;

	// Why a request was not queued, and what to try next.
	struct Refusal
	{
		std::string message;
		std::string hint;
	};

	// Completion subscribers; any thread may add or notify.
	template <class Completion>
	class ObserverList
	{
	public:
		using Observer = std::function<void(const Completion&)>;

		void Add(Observer a_observer)
		{
			const std::scoped_lock lock{ _mutex };
			_observers.push_back(std::move(a_observer));
		}

		void Notify(const Completion& a_completion) const
		{
			std::vector<Observer> observers;
			{
				const std::scoped_lock lock{ _mutex };
				observers = _observers;
			}
			for (const auto& observer : observers) {
				try {
					observer(a_completion);
				} catch (const std::exception& error) {
					log::Get("cs.capture")->error("Capture observer threw: {}", error.what());
				} catch (...) {
					log::Get("cs.capture")->error("Capture observer threw");
				}
			}
		}

	private:
		mutable std::mutex _mutex;
		std::vector<Observer> _observers;
	};
}
