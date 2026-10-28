#include "World/GameTime.h"

#include "Log.h"

#include "RE/C/Calendar.h"
#include "RE/L/LoadingMenu.h"
#include "RE/P/PipboyMenu.h"
#include "RE/T/TESGlobal.h"
#include "RE/U/UI.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <string_view>

namespace cs::engine
{
	namespace
	{
		auto* L = log::Get("cs.weather");

		struct State
		{
			std::mutex mutex;
			float savedTimeScale = kVanillaTimeScale;
			bool paused = false;
			bool restoredForMenu = false;
			bool wasPausedBeforeMenu = false;
		};

		State& GetState()
		{
			static State state;
			return state;
		}

		RE::TESGlobal* TimeScaleGlobal()
		{
			const auto* calendar = RE::Calendar::GetSingleton();
			return calendar ? calendar->timeScale : nullptr;
		}

		void PauseLocked(State& a_state)
		{
			auto* timeScale = TimeScaleGlobal();
			if (a_state.paused || !timeScale)
				return;
			a_state.savedTimeScale = timeScale->value;
			timeScale->value = 0.0f;
			a_state.paused = true;
			L->info("Time paused (saved timescale: {})", a_state.savedTimeScale);
		}

		void ResumeLocked(State& a_state)
		{
			auto* timeScale = TimeScaleGlobal();
			if (!a_state.paused || !timeScale)
				return;
			timeScale->value = a_state.savedTimeScale;
			a_state.paused = false;
			L->info("Time resumed (timescale: {})", a_state.savedTimeScale);
		}

		// FO4: Pip-Boy launches fast travel; time must run or these menus hang.
		constexpr std::array<std::string_view, 3> kTimeSensitiveMenus{
			RE::LoadingMenu::MENU_NAME, "SleepWaitMenu", RE::PipboyMenu::MENU_NAME
		};

		bool IsTimeSensitiveMenu(const RE::BSFixedString& a_menuName)
		{
			return std::ranges::any_of(kTimeSensitiveMenus, [&](std::string_view a_name) { return a_menuName == a_name; });
		}

		bool IsAnyTimeSensitiveMenuOpen()
		{
			const auto* ui = RE::UI::GetSingleton();
			return ui && std::ranges::any_of(kTimeSensitiveMenus, [&](std::string_view a_name) { return ui->GetMenuOpen(a_name); });
		}

		void SetTimeRunningForMenu(bool a_needsRunningTime)
		{
			auto& state = GetState();
			const std::scoped_lock lock(state.mutex);
			auto* timeScale = TimeScaleGlobal();
			if (!timeScale)
				return;
			if (a_needsRunningTime) {
				if (state.restoredForMenu || timeScale->value != 0.0f)
					return;
				// Re-pause only our own pause; a foreign zero timescale stays restored.
				state.wasPausedBeforeMenu = state.paused;
				// A non-positive snapshot would restore straight back to frozen time.
				if (state.savedTimeScale <= 0.0f)
					state.savedTimeScale = kVanillaTimeScale;
				if (state.paused)
					ResumeLocked(state);
				else
					timeScale->value = std::max(state.savedTimeScale, kVanillaTimeScale);
				state.restoredForMenu = true;
			} else if (state.restoredForMenu) {
				if (state.wasPausedBeforeMenu)
					PauseLocked(state);
				state.restoredForMenu = false;
				state.wasPausedBeforeMenu = false;
			}
		}

		class MenuGuard final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				// Menus overlap in fast travel; end the guard once all are closed.
				if (IsTimeSensitiveMenu(a_event.menuName))
					SetTimeRunningForMenu(a_event.opening || IsAnyTimeSensitiveMenuOpen());
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void InstallGameTimeMenuGuard()
	{
		static MenuGuard guard;
		if (auto* ui = RE::UI::GetSingleton())
			ui->RegisterSink<RE::MenuOpenCloseEvent>(&guard);
		else
			L->error("UI event source unavailable; pausing time can hang fast travel");
	}

	void PauseGameTime()
	{
		auto& state = GetState();
		const std::scoped_lock lock(state.mutex);
		PauseLocked(state);
	}

	void ResumeGameTime()
	{
		auto& state = GetState();
		const std::scoped_lock lock(state.mutex);
		ResumeLocked(state);
	}

	bool IsGameTimePaused() noexcept
	{
		auto& state = GetState();
		const std::scoped_lock lock(state.mutex);
		// An external timescale change overrides our pause.
		if (const auto* timeScale = TimeScaleGlobal(); state.paused && timeScale && timeScale->value > 0.0f)
			state.paused = false;
		return state.paused;
	}

	std::optional<float> GetTimeScale()
	{
		auto& state = GetState();
		const std::scoped_lock lock(state.mutex);
		const auto* timeScale = TimeScaleGlobal();
		if (!timeScale)
			return std::nullopt;
		return state.paused ? state.savedTimeScale : timeScale->value;
	}

	void SetTimeScale(float a_scale)
	{
		auto& state = GetState();
		const std::scoped_lock lock(state.mutex);
		if (state.paused)
			state.savedTimeScale = a_scale;
		else if (auto* timeScale = TimeScaleGlobal())
			timeScale->value = a_scale;
	}

	void ResetTimeScale()
	{
		SetTimeScale(kVanillaTimeScale);
	}

	std::optional<float> GetGameHour()
	{
		const auto* calendar = RE::Calendar::GetSingleton();
		if (!calendar || !calendar->gameHour)
			return std::nullopt;
		return calendar->gameHour->value;
	}

	void SetGameHour(float a_hour)
	{
		auto* calendar = RE::Calendar::GetSingleton();
		if (!calendar || !calendar->gameHour)
			return;
		calendar->gameHour->value = a_hour;
	}
}
