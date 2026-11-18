#include "Render/FrameState.h"

#include "Host/HostClient.h"
#include "Render/Engine.h"

#include <atomic>
#include <mutex>

namespace cs::render
{
	namespace
	{
		std::mutex g_mutex;
		FrameState g_latched;
		bool g_hasLatch = false;
		std::uint32_t g_worldEngineFrame = 0;
		std::uint32_t g_resetEngineFrame = 0;
		std::uint8_t g_resetBits = 0;
		std::atomic_uint8_t g_pendingResets{ 0 };
		std::atomic_uint64_t g_worldFrame{ 0 };

		// Sampled outside the lock: DearModdingUI callbacks can re-enter from its own lock.
		FrameState SampleMenuState(std::uint32_t a_frame) noexcept
		{
			const auto* ui = RE::UI::GetSingleton();
			FrameState state;
			state.engineFrame = a_frame;
			state.mainOrLoading = ui && (ui->GetMenuOpen<RE::MainMenu>() || ui->GetMenuOpen<RE::LoadingMenu>());
			state.paused = !ui || ui->GameIsPaused();
			state.shellOpen = host::HostClient::IsShellOpen();
			return state;
		}

		FrameState Compose(std::uint32_t a_frame) noexcept
		{
			auto state = g_latched;
			state.worldFrame = g_worldFrame.load(std::memory_order_acquire);
			state.worldRendered = state.worldFrame != 0 && g_worldEngineFrame == a_frame;
			state.resets = g_resetEngineFrame == a_frame ? g_resetBits : std::uint8_t{ 0 };
			return state;
		}

		bool Latched(std::uint32_t a_frame) noexcept
		{
			return g_hasLatch && g_latched.engineFrame == a_frame;
		}
	}

	FrameState GetFrameState() noexcept
	{
		const auto frame = engine::GetFrameCount();
		{
			std::scoped_lock lock(g_mutex);
			if (Latched(frame))
				return Compose(frame);
		}
		const auto sampled = SampleMenuState(frame);
		std::scoped_lock lock(g_mutex);
		if (!Latched(frame)) {
			g_latched = sampled;
			g_hasLatch = true;
		}
		return Compose(frame);
	}

	void RequestReset(ResetReason a_reason) noexcept
	{
		g_pendingResets.fetch_or(static_cast<std::uint8_t>(a_reason), std::memory_order_acq_rel);
	}

	std::uint64_t BeginWorldFrame() noexcept
	{
		const auto state = GetFrameState();
		const auto world = g_worldFrame.fetch_add(1, std::memory_order_acq_rel) + 1;
		std::scoped_lock lock(g_mutex);
		g_worldEngineFrame = state.engineFrame;
		if (!state.mainOrLoading) {
			if (const auto bits = g_pendingResets.exchange(0, std::memory_order_acq_rel)) {
				g_resetBits = bits;
				g_resetEngineFrame = state.engineFrame;
			}
		}
		return world;
	}
}
