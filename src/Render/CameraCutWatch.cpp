#include "Render/CameraCutWatch.h"

#include "Log.h"
#include "PCH.h"
#include "Render/FrameBoundary.h"
#include "Render/FrameState.h"

#include "RE/P/PlayerCamera.h"

#include <optional>

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.camera");

		// Continuous camera motion; every other state change is a snap cut.
		constexpr bool IsContinuous(RE::CameraState a_state) noexcept
		{
			using enum RE::CameraState;
			return a_state == kIronSights || a_state == kTween || a_state == kPCTransition;
		}

		constexpr bool IsCameraCut(RE::CameraState a_from, RE::CameraState a_to) noexcept
		{
			return !IsContinuous(a_from) && !IsContinuous(a_to);
		}

		// Main thread only.
		std::optional<RE::CameraState> g_lastState;

		void PollCameraState()
		{
			const auto* camera = RE::PlayerCamera::GetSingleton();
			const auto* state = camera ? camera->currentState.get() : nullptr;
			if (!state) {
				g_lastState.reset();
				return;
			}
			const auto to = state->id.get();
			if (g_lastState && *g_lastState != to) {
				const auto from = *g_lastState;
				const bool cut = IsCameraCut(from, to);
				L->debug("Camera state {} -> {}: reset {}", magic_enum::enum_name(from), magic_enum::enum_name(to), cut ? "yes" : "no");
				if (cut)
					RequestReset(ResetReason::kCameraState);
			}
			g_lastState = to;
		}
	}

	bool RegisterCameraCutWatch()
	{
		// The engine changes the state outside the one function we could detour.
		return RegisterPreSwap(&PollCameraState);
	}
}
