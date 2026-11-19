#include "Render/FrameState.h"

#include "Host/HostClient.h"
#include "Render/Engine.h"

#include "RE/L/LoadingMenu.h"
#include "RE/M/MainMenu.h"
#include "RE/P/PipboyMenu.h"
#include "RE/U/UI.h"

#include <array>
#include <atomic>
#include <string_view>

namespace cs::render
{
	namespace
	{
		// Frame in the high word, flags in the low byte.
		constexpr std::uint8_t kShellOpen = 1 << 0;
		constexpr std::uint8_t kPaused = 1 << 1;
		constexpr std::uint8_t kMainOrLoading = 1 << 2;
		constexpr std::uint8_t kWorldFrozen = 1 << 3;
		constexpr std::uint8_t kLatched = 1 << 7;

		constexpr std::uint64_t Pack(std::uint32_t a_frame, std::uint8_t a_bits) noexcept
		{
			return (static_cast<std::uint64_t>(a_frame) << 32) | a_bits;
		}

		constexpr std::uint32_t FrameOf(std::uint64_t a_packed) noexcept
		{
			return static_cast<std::uint32_t>(a_packed >> 32);
		}

		constexpr std::uint8_t BitsOf(std::uint64_t a_packed) noexcept
		{
			return static_cast<std::uint8_t>(a_packed);
		}

		constexpr std::uint8_t kAllMenus = 0xFF;

		struct TrackedName
		{
			std::string_view name;
			TrackedMenu menu;
		};

		constexpr std::array kTrackedNames{
			TrackedName{ RE::MainMenu::MENU_NAME, TrackedMenu::kMain },
			TrackedName{ RE::LoadingMenu::MENU_NAME, TrackedMenu::kLoading },
			TrackedName{ "SleepWaitMenu", TrackedMenu::kSleepWait },
			TrackedName{ RE::PipboyMenu::MENU_NAME, TrackedMenu::kPipboy }
		};

		constexpr std::uint8_t Bit(TrackedMenu a_menu) noexcept
		{
			return static_cast<std::uint8_t>(a_menu);
		}

		std::atomic_uint8_t g_openMenus{ kAllMenus };
		std::array<std::atomic<MenuListener>, 4> g_listeners{};

		void SetOpen(TrackedMenu a_menu, bool a_open) noexcept
		{
			if (a_open)
				g_openMenus.fetch_or(Bit(a_menu), std::memory_order_acq_rel);
			else
				g_openMenus.fetch_and(static_cast<std::uint8_t>(~Bit(a_menu)), std::memory_order_acq_rel);
		}

		class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				for (const auto& tracked : kTrackedNames) {
					if (a_event.menuName != tracked.name)
						continue;
					SetOpen(tracked.menu, a_event.opening);
					if (tracked.menu == TrackedMenu::kLoading && !a_event.opening)
						RequestReset(ResetReason::kLoad);
					for (const auto& slot : g_listeners) {
						if (const auto listener = slot.load(std::memory_order_acquire))
							listener(tracked.menu, a_event.opening);
					}
					break;
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		std::atomic_uint64_t g_menu{ 0 };
		std::atomic_uint64_t g_resets{ 0 };
		std::atomic_uint8_t g_pendingResets{ 0 };
		std::atomic_uint64_t g_worldFrame{ 0 };

		bool Latched(std::uint64_t a_packed, std::uint32_t a_frame) noexcept
		{
			return (BitsOf(a_packed) & kLatched) != 0 && FrameOf(a_packed) == a_frame;
		}

		std::uint8_t SampleMenuBits() noexcept
		{
			const auto* ui = RE::UI::GetSingleton();
			const auto* main = RE::Main::GetSingleton();
			std::uint8_t bits = kLatched;
			if (IsAnyMenuOpen({ TrackedMenu::kMain, TrackedMenu::kLoading }))
				bits |= kMainOrLoading;
			if (!ui || ui->GameIsPaused())
				bits |= kPaused;
			if (host::HostClient::IsShellOpen())
				bits |= kShellOpen;
			if (main && main->gameActive && (main->inMenuMode || main->freezeTime))
				bits |= kWorldFrozen;
			return bits;
		}

		FrameState Compose(std::uint64_t a_menu, std::uint32_t a_frame) noexcept
		{
			const auto bits = BitsOf(a_menu);
			FrameState state;
			state.engineFrame = a_frame;
			state.shellOpen = (bits & kShellOpen) != 0;
			state.paused = (bits & kPaused) != 0;
			state.mainOrLoading = (bits & kMainOrLoading) != 0;
			state.worldFrozen = (bits & kWorldFrozen) != 0;
			state.worldFrame = g_worldFrame.load(std::memory_order_acquire);
			const auto resets = g_resets.load(std::memory_order_acquire);
			state.resets = FrameOf(resets) == a_frame ? BitsOf(resets) : std::uint8_t{ 0 };
			return state;
		}
	}

	void AddMenuListener(MenuListener a_listener) noexcept
	{
		for (auto& slot : g_listeners) {
			MenuListener expected = nullptr;
			if (slot.compare_exchange_strong(expected, a_listener, std::memory_order_acq_rel) || expected == a_listener)
				return;
		}
	}

	bool RegisterMenuEvents()
	{
		static MenuEventSink sink;
		static bool registered = false;
		if (registered)
			return true;
		auto* ui = RE::UI::GetSingleton();
		if (!ui)
			return false;
		// Menus already open before registration produce no event.
		std::uint8_t open = 0;
		for (const auto& tracked : kTrackedNames) {
			if (ui->GetMenuOpen(tracked.name))
				open |= Bit(tracked.menu);
		}
		g_openMenus.store(open, std::memory_order_release);
		ui->RegisterSink<RE::MenuOpenCloseEvent>(&sink);
		registered = true;
		return true;
	}

	bool IsAnyMenuOpen(std::initializer_list<TrackedMenu> a_menus) noexcept
	{
		std::uint8_t mask = 0;
		for (const auto menu : a_menus)
			mask |= Bit(menu);
		return (g_openMenus.load(std::memory_order_acquire) & mask) != 0;
	}

	FrameState GetFrameState() noexcept
	{
		const auto frame = engine::GetFrameCount();
		auto menu = g_menu.load(std::memory_order_acquire);
		if (!Latched(menu, frame)) {
			// First sampler wins; lock-free because UI callbacks can re-enter.
			const auto sampled = Pack(frame, SampleMenuBits());
			while (!Latched(menu, frame)) {
				if (g_menu.compare_exchange_weak(menu, sampled, std::memory_order_acq_rel)) {
					menu = sampled;
					break;
				}
			}
		}
		return Compose(menu, frame);
	}

	void RequestReset(ResetReason a_reason) noexcept
	{
		g_pendingResets.fetch_or(static_cast<std::uint8_t>(a_reason), std::memory_order_acq_rel);
	}

	void BeginWorldFrame() noexcept
	{
		const auto state = GetFrameState();
		g_worldFrame.fetch_add(1, std::memory_order_acq_rel);
		if (!state.mainOrLoading) {
			if (const auto bits = g_pendingResets.exchange(0, std::memory_order_acq_rel))
				g_resets.store(Pack(state.engineFrame, bits), std::memory_order_release);
		}
	}
}
