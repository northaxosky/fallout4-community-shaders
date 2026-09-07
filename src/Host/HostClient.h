#pragma once

#include "Host/HostPageCatalog.h"
#include "Host/HostRuntimeModel.h"

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
			std::uint32_t a_durationMilliseconds) noexcept;
		bool DrawAnnotatedPlot(
			const char* a_id,
			const DMUI_AnnotatedPlotDescriptor& a_descriptor) noexcept;

	private:
		HostClient();

		struct Page
		{
			HostPageDescriptor descriptor;
			Feature* feature{};
			DMUI_PageHandle handle{ DMUI_INVALID_PAGE_HANDLE };
		};

		struct PendingNotification
		{
			DMUI_StatusSeverity severity{ DMUI_STATUS_SEVERITY_INFO };
			std::string message;
			std::uint32_t durationMilliseconds{ 4000 };
		};

		bool RegisterPages();
		bool RegisterActionsAndObservers();
		bool RegisterHotkeys();
		void PublishInitialStatus() noexcept;
		void DrawPage(Page& a_page);
		void DrawFeaturePage(Feature& a_feature);
		void DrawOverlayPage();
		void ObserveFrame() noexcept;
		void ObservePageActivity(const dmui::PageActivity& a_activity) noexcept;
		void SyncOverlay() noexcept;
		void ObserveHotkeyBindings() noexcept;
		void ObserveHotkeyBinding(
			const char* a_name,
			const std::optional<DMUI_HotkeyActionHandle>& a_handle,
			std::string& a_snapshot) noexcept;
		void SetHotkeyEnabled(
			const char* a_name,
			const std::optional<DMUI_HotkeyActionHandle>& a_handle,
			bool a_enabled,
			std::optional<DMUI_Result>& a_failure) noexcept;
		void FlushNotification() noexcept;
		void RetrySwapChain() noexcept;
		void LogFailure(std::string_view a_operation) const noexcept;
		void LogFailureOnce(
			std::string_view a_operation,
			std::optional<DMUI_Result>& a_lastResult) const noexcept;

		dmui::Client _client;
		std::vector<std::unique_ptr<Page>> _pages;
		Page* _overlayPage{};
		std::optional<DMUI_HotkeyActionHandle> _overlayHotkey;
		std::optional<DMUI_HotkeyActionHandle> _captureHotkey;
		std::optional<DMUI_HotkeyActionHandle> _multiCaptureHotkey;
		std::optional<DMUI_HotkeyActionHandle> _dumpHotkey;
		std::array<std::string, 4> _hotkeyBindingSnapshots;
		std::array<std::optional<DMUI_Result>, 3> _hotkeyEnableFailures;
		std::optional<DMUI_Result> _videoMemoryFailure;
		std::optional<DMUI_Result> _overlayConfigurationFailure;
		std::optional<DMUI_Result> _overlayDemandFailure;
		std::optional<DMUI_Result> _overlayQueryFailure;
		std::optional<DMUI_Result> _annotatedPlotFailure;
		std::atomic_bool _overlayVisible{ true };
		std::atomic_bool _registrationComplete{};
		FrameDemandTracker _overlayFrameDemand;
		std::atomic_bool _readyLogged{};
		std::atomic_bool _unavailableLogged{};
		std::mutex _notificationMutex;
		std::optional<PendingNotification> _pendingNotification;
		std::mutex _swapChainMutex;
		IDXGISwapChain* _pendingSwapChain{};
	};
}
