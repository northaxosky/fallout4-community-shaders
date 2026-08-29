#pragma once

#include "Host/HostPageCatalog.h"
#include "Host/IntegrationState.h"
#include "Host/OverlayDemandModel.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <DearModdingUI/API.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
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

		IntegrationState GetState() const noexcept { return _state.Get(); }
		bool IsHosted() const noexcept { return _state.IsHosted(); }

		bool OnD3D11Bootstrap(
			ID3D11Device* a_device,
			ID3D11DeviceContext* a_context,
			IDXGISwapChain* a_swapChain,
			HWND a_window) noexcept;

		bool IsHostMenuVisible() const noexcept;

		void SyncOverlayDemand() noexcept;

	private:
		HostClient() = default;

		struct Page
		{
			HostPageDescriptor descriptor;
			Feature* feature{ nullptr };
			DMUI_PageHandle handle{ DMUI_INVALID_PAGE_HANDLE };
		};

		bool Register(const DMUI_HostAPI& a_api, const std::string& a_modulePath) noexcept;
		bool RegisterPages() noexcept;
		void FallBackToStandalone(std::string_view a_reason) noexcept;

		void OnHostReady(const DMUI_HostReadyInfo* a_info) noexcept;
		void OnHostUnavailable(DMUI_UnavailableReason a_reason) noexcept;
		void GoUnavailable(DMUI_UnavailableReason a_reason, bool a_allowFallback) noexcept;
		void DrawPage(Page& a_page) noexcept;

		static void DMUI_CALL ReadyCallback(const DMUI_HostReadyInfo* a_info, void* a_userData) noexcept;
		static void DMUI_CALL UnavailableCallback(DMUI_UnavailableReason a_reason, void* a_userData) noexcept;
		static void DMUI_CALL DrawCallback(void* a_userData) noexcept;

		void SaveFallbackResources(
			ID3D11Device* a_device,
			ID3D11DeviceContext* a_context,
			IDXGISwapChain* a_swapChain,
			HWND a_window) noexcept;
		void ReleaseFallbackResourcesLocked() noexcept;
		void ReleaseFallbackResources() noexcept;
		void StartStandaloneFallback() noexcept;
		bool OverlayWanted() const noexcept;

		IntegrationStateMachine _state;

		const DMUI_HostAPI* _api{ nullptr };
		DMUI_ClientHandle _client{ DMUI_INVALID_CLIENT_HANDLE };
		Page* _overlayPage{ nullptr };
		std::vector<std::unique_ptr<Page>> _pages;
		std::vector<Feature*> _features;
		DMUI_ClientDescriptor _clientDescriptor{};
		std::string _clientId;
		std::string _clientDisplayName;

		mutable std::mutex _demandMutex;
		OverlayDemandModel _overlayDemand;

		std::mutex _fallbackMutex;
		ID3D11Device* _fallbackDevice{ nullptr };
		ID3D11DeviceContext* _fallbackContext{ nullptr };
		IDXGISwapChain* _fallbackSwapChain{ nullptr };
		HWND _fallbackWindow{ nullptr };
		bool _bootstrapSeen{ false };
		bool _standaloneStarted{ false };
	};
}
