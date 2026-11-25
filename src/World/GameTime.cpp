#include "World/GameTime.h"

#include "Log.h"
#include "Render/FrameState.h"

#include "RE/C/Calendar.h"
#include "RE/T/TESGlobal.h"

#include <algorithm>
#include <mutex>

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
		bool IsAnyTimeSensitiveMenuOpen()
		{
			using render::TrackedMenu;
			return render::IsAnyMenuOpen({ TrackedMenu::kLoading, TrackedMenu::kSleepWait, TrackedMenu::kPipboy });
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

		void OnTrackedMenu(render::TrackedMenu a_menu, bool a_opening)
		{
			if (a_menu == render::TrackedMenu::kMain)
				return;
			// Menus overlap in fast travel; end the guard once all are closed.
			SetTimeRunningForMenu(a_opening || IsAnyTimeSensitiveMenuOpen());
		}
	}

	void InstallGameTimeMenuGuard()
	{
		render::AddMenuListener(&OnTrackedMenu);
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
