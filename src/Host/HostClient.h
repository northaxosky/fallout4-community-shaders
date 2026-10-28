#pragma once

#include "Host/HostPageCatalog.h"
#include "Host/ShaderCompileOverlay.h"

#include <DearModdingUI/Client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct ID3D11Device;
struct IDXGISwapChain;

namespace cs
{
	class Feature;
}

namespace cs::host
{
	class HostClient
	{
	public:
		static HostClient& Get();

		HostClient(const HostClient&) = delete;
		HostClient& operator=(const HostClient&) = delete;

		void DiscoverAndRegister() noexcept;
		void OnD3D11Bootstrap(
			ID3D11Device* a_device,
			IDXGISwapChain* a_swapChain,
			HWND a_window) noexcept;

		[[nodiscard]] dmui::Client& Client() noexcept { return _client; }

		void PostNotification(
			DMUI_StatusSeverity a_severity,
			std::string a_message,
			std::uint32_t a_durationMilliseconds,
			std::string a_title = {}) noexcept;
		void ResetOverlay() noexcept;

	private:
		HostClient();

		enum class FrameDemandAction : std::uint8_t
		{
			kNone,
			kRequest,
			kRelease
		};

		class FrameDemandTracker
		{
		public:
			[[nodiscard]] FrameDemandAction Next(bool a_wanted) const noexcept;
			void Complete(FrameDemandAction a_action, bool a_succeeded) noexcept;
			[[nodiscard]] bool Requested() const noexcept { return _requested; }

		private:
			bool _requested{};
		};

		struct Page
		{
			HostPageDescriptor descriptor;
			Feature* feature{};
			DMUI_PageHandle handle{ DMUI_INVALID_PAGE_HANDLE };
		};

		struct OverlayChannel
		{
			Page* page{};
			FrameDemandTracker demand;
			std::optional<DMUI_Result> configurationFailure;
			std::optional<DMUI_Result> demandFailure;
		};

		struct HotkeyChannel
		{
			const char* name;
			std::optional<DMUI_HotkeyActionHandle> handle;
			std::string bindingSnapshot;
			std::optional<DMUI_Result> enableFailure;
			// Last state the host accepted; the host is called only on change.
			std::optional<bool> appliedEnabled;
			std::function<bool()> enabled;
		};

		enum Hotkey : std::size_t
		{
			kOverlay,
			kMultiCapture,
			kCapture,
			kDump,
			kHotkeyCount
		};

		bool RegisterPages();
		bool RegisterActionsAndObservers();
		bool RegisterHotkeys();
		bool RegisterHotkey(
			HotkeyChannel& a_channel,
			const char* a_id,
			const char* a_displayName,
			const char* a_suggestedChord,
			std::function<void(bool)> a_callback,
			std::function<bool()> a_enabled,
			DMUI_HotkeyContextPolicy a_contextPolicy = DMUI_HOTKEY_CONTEXT_HOST_INPUT_INACTIVE);
		void PublishInitialStatus() noexcept;
		void DrawPage(Page& a_page);
		void DrawFeaturePage(Feature& a_feature);
		void DrawOverlayPage();
		void DrawShaderCompileOverlayPage();
		void ObserveFrame() noexcept;
		void ObservePageActivity(const dmui::PageActivity& a_activity) noexcept;
		void SyncPerformanceOverlay() noexcept;
		void SyncShaderCompileOverlay() noexcept;
		void SyncOverlay(
			OverlayChannel& a_overlay,
			std::string_view a_name,
			bool a_wanted,
			const DMUI_ManagedOverlayOptions& a_options) noexcept;
		void ObserveHotkeyBindings() noexcept;
		void ObserveHotkeyBinding(
			HotkeyChannel& a_channel) noexcept;
		void SetHotkeyEnabled(HotkeyChannel& a_channel) noexcept;
		void RetrySwapChain() noexcept;
		void LogFailure(std::string_view a_operation) const noexcept;
		void LogFailureOnce(
			std::string_view a_operation,
			std::optional<DMUI_Result>& a_lastResult) const noexcept;

		dmui::Client _client;
		std::vector<std::unique_ptr<Page>> _pages;
		OverlayChannel _performanceOverlay;
		OverlayChannel _shaderCompileOverlay;
		ShaderCompileOverlay _shaderCompile;
		std::array<HotkeyChannel, kHotkeyCount> _hotkeys{
			HotkeyChannel{ "Performance Overlay" },
			HotkeyChannel{ "RenderDoc multi-frame capture" },
			HotkeyChannel{ "RenderDoc single-frame capture" },
			HotkeyChannel{ "telemetry dump" }
		};
		std::optional<DMUI_Result> _videoMemoryFailure;
		std::uint32_t _bindingPollCountdown{};
		std::atomic_bool _overlayVisible{ false };
		std::atomic_bool _registrationComplete{};
		std::atomic_bool _readyLogged{};
		std::atomic_bool _unavailableLogged{};
		std::mutex _swapChainMutex;
		IDXGISwapChain* _pendingSwapChain{};
	};
}
