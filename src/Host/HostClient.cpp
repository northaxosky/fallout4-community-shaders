#include "Host/HostClient.h"

#include "Feature.h"
#include "Host/HostDiscovery.h"
#include "Host/HostFingerprint.h"
#include "Host/SwapChainHandoff.h"
#include "Log.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/ImGuiRecovery.h"
#include "Menu/Menu.h"
#include "Menu/OverlayRenderer.h"
#include "Plugin.h"

#include <exception>

#include <d3d11.h>
#include <dxgi.h>

namespace cs::host
{
	namespace
	{
		auto* L = cs::log::Get("cs.host");

		constexpr std::string_view kClientId = "dearmodding.community-shaders";
		constexpr std::string_view kClientDisplayName = "Community Shaders";

		std::string_view DescribeResult(DMUI_Result a_result) noexcept
		{
			switch (a_result) {
			case DMUI_RESULT_OK:
				return "ok";
			case DMUI_RESULT_UNSUPPORTED_ABI:
				return "unsupported ABI";
			case DMUI_RESULT_INVALID_ARGUMENT:
				return "invalid argument";
			case DMUI_RESULT_STRUCT_TOO_SMALL:
				return "struct too small";
			case DMUI_RESULT_INVALID_DESCRIPTOR:
				return "invalid descriptor";
			case DMUI_RESULT_FINGERPRINT_MISMATCH:
				return "Dear ImGui fingerprint mismatch";
			case DMUI_RESULT_DUPLICATE_CLIENT_ID:
				return "duplicate client id";
			case DMUI_RESULT_DUPLICATE_PAGE_ID:
				return "duplicate page id";
			case DMUI_RESULT_REGISTRATION_CLOSED:
				return "registration closed";
			case DMUI_RESULT_HOST_DISABLED:
				return "host disabled";
			case DMUI_RESULT_HOST_NOT_INITIALIZED:
				return "host not initialized";
			case DMUI_RESULT_HOST_NOT_READY:
				return "host not ready";
			case DMUI_RESULT_BACKEND_FAILED:
				return "host backend failed";
			case DMUI_RESULT_RESOURCE_EXHAUSTED:
				return "resource exhausted";
			case DMUI_RESULT_CLIENT_CAPABILITY_REQUIRED:
				return "renderer replacement capability required";
			case DMUI_RESULT_SWAPCHAIN_REJECTED:
				return "swapchain rejected";
			case DMUI_RESULT_RENDERER_BUSY:
				return "host renderer busy";
			default:
				return "unknown error";
			}
		}

		std::string_view DescribeUnavailable(DMUI_UnavailableReason a_reason) noexcept
		{
			switch (a_reason) {
			case DMUI_UNAVAILABLE_HOST_DISABLED:
				return "the host is disabled";
			case DMUI_UNAVAILABLE_BACKEND_FAILED:
				return "the host renderer failed to initialize";
			default:
				return "no reason given";
			}
		}

		bool ReadyInfoIsUsable(const DMUI_HostReadyInfo* a_info) noexcept
		{
			return a_info &&
			       a_info->structSize >= sizeof(DMUI_HostReadyInfo) &&
			       a_info->apiVersion == DMUI_API_VERSION_CURRENT &&
			       a_info->imguiContext &&
			       a_info->imguiAlloc &&
			       a_info->imguiFree;
		}

		FeaturePageInput DescribeFeature(Feature& a_feature) noexcept
		{
			FeaturePageInput input;
			try {
				input.name = std::string(a_feature.GetName());
				input.displayName = std::string(a_feature.GetDisplayName());
				input.category = a_feature.GetCategory();
				input.summary = a_feature.GetFeatureSummary();
				input.active = a_feature.IsActive();
				input.installed = a_feature.IsInstalled();
			} catch (...) {
				L->warn("A feature failed to provide Dear-Modding UI page metadata");
			}
			return input;
		}
	}

	HostClient& HostClient::Get()
	{
		static HostClient instance;
		return instance;
	}

	void HostClient::DiscoverAndRegister() noexcept
	{
		try {
			const auto discovered = DiscoverHost(ClientFingerprint());
			if (!discovered || !discovered->api) {
				FallBackToStandalone("no compatible Dear-Modding UI host is loaded");
				return;
			}
			if (!Register(*discovered->api, discovered->modulePath))
				return;
			L->info("Registered {} pages with the Dear-Modding UI host {}",
				_pages.size(), discovered->modulePath);
		} catch (const std::exception& e) {
			FallBackToStandalone(e.what());
		} catch (...) {
			FallBackToStandalone("non-standard exception during host discovery");
		}
	}

	bool HostClient::Register(const DMUI_HostAPI& a_api, const std::string& a_modulePath) noexcept
	{
		try {
			_api = &a_api;
			_clientId = std::string(kClientId);
			_clientDisplayName = std::string(kClientDisplayName);
			_clientDescriptor = DMUI_ClientDescriptor{
				sizeof(DMUI_ClientDescriptor),
				DMUI_API_VERSION_CURRENT,
				_clientId.c_str(),
				_clientDisplayName.c_str(),
				DMUI_MAKE_VERSION(Plugin::VERSION[0], Plugin::VERSION[1]),
				&ClientFingerprint(),
				&HostClient::ReadyCallback,
				&HostClient::UnavailableCallback,
				this,
				DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT
			};

			if (!_state.ChooseRegistered()) {
				_api = nullptr;
				return false;
			}

			const auto result = a_api.registerClient(&_clientDescriptor, &_client);
			if (result != DMUI_RESULT_OK) {
				_client = DMUI_INVALID_CLIENT_HANDLE;
				FallBackToStandalone(DescribeResult(result));
				return false;
			}
			if (!RegisterPages())
				return false;

			L->info("Hosted by {} as '{}'", a_modulePath, _clientId);
			return true;
		} catch (...) {
			FallBackToStandalone("non-standard exception during host registration");
			return false;
		}
	}

	bool HostClient::RegisterPages() noexcept
	{
		std::vector<FeaturePageInput> inputs;
		std::vector<Feature*> features;
		for (Feature* feature : FeatureManager::Get().GetRegisteredFeatures()) {
			if (!feature || !feature->IsInMenu())
				continue;
			inputs.push_back(DescribeFeature(*feature));
			features.push_back(feature);
		}

		for (auto& descriptor : BuildPageCatalog(inputs)) {
			auto page = std::make_unique<Page>();
			page->feature = descriptor.kind == HostPageKind::kFeature ?
			                    features[descriptor.featureIndex] :
			                    nullptr;
			page->descriptor = std::move(descriptor);

			DMUI_PageDescriptor pageDescriptor{
				sizeof(DMUI_PageDescriptor),
				page->descriptor.id.c_str(),
				page->descriptor.displayName.c_str(),
				page->descriptor.category.c_str(),
				page->descriptor.summary.empty() ? nullptr : page->descriptor.summary.c_str(),
				page->descriptor.sortKey,
				page->descriptor.kind == HostPageKind::kOverlay ?
					DMUI_PAGE_KIND_OVERLAY :
					DMUI_PAGE_KIND_SETTINGS,
				&HostClient::DrawCallback,
				page.get()
			};

			const auto result = _api->registerPage(_client, &pageDescriptor, &page->handle);
			if (result != DMUI_RESULT_OK) {
				FallBackToStandalone(DescribeResult(result));
				return false;
			}
			if (page->descriptor.kind == HostPageKind::kOverlay)
				_overlayPage = page.get();
			_pages.push_back(std::move(page));
		}

		_features = std::move(features);
		return true;
	}

	void HostClient::FallBackToStandalone(std::string_view a_reason) noexcept
	{
		FallbackResources resources;
		bool changed = false;
		bool bootstrapSeen = false;
		bool startFallback = false;
		{
			const std::scoped_lock lock{ _fallbackMutex };
			// DMUI v1 cannot unregister an accepted client.
			changed = _state.ChooseStandalone() || _state.ChooseStandaloneFromRegistered();
			bootstrapSeen = _fallbackCoordination.BootstrapSeen();
			if (changed &&
				_fallbackCoordination.OnStandaloneTransition() ==
					FallbackAction::kStandaloneFromSavedResources) {
				resources = TakeFallbackResourcesLocked();
				startFallback = true;
			}
			ReleasePendingSwapChainLocked();
		}
		if (changed)
			L->info("Community Shaders owns its menu: {}", a_reason);
		if (startFallback)
			StartStandaloneFallback(resources);
		else if (changed && bootstrapSeen)
			L->error("No saved renderer resources; the standalone menu cannot start");
	}

	void DMUI_CALL HostClient::ReadyCallback(const DMUI_HostReadyInfo* a_info, void* a_userData) noexcept
	{
		if (auto* client = static_cast<HostClient*>(a_userData))
			client->OnHostReady(a_info);
	}

	void DMUI_CALL HostClient::UnavailableCallback(DMUI_UnavailableReason a_reason, void* a_userData) noexcept
	{
		if (auto* client = static_cast<HostClient*>(a_userData))
			client->OnHostUnavailable(a_reason);
	}

	void DMUI_CALL HostClient::DrawCallback(void* a_userData) noexcept
	{
		if (auto* page = static_cast<Page*>(a_userData))
			Get().DrawPage(*page);
	}

	void HostClient::OnHostReady(const DMUI_HostReadyInfo* a_info) noexcept
	{
		try {
			if (!ReadyInfoIsUsable(a_info)) {
				// Never contest a host that claimed ImGui ownership.
				L->error("The Dear-Modding UI host published unusable ready information");
				GoUnavailable(DMUI_UNAVAILABLE_BACKEND_FAILED, false);
				return;
			}

			FallbackResources resources;
			{
				const std::scoped_lock lock{ _fallbackMutex };
				if (_state.Get() != IntegrationState::kRegisteredWaiting)
					return;

				ImGui::SetCurrentContext(static_cast<ImGuiContext*>(a_info->imguiContext));
				ImGui::SetAllocatorFunctions(
					a_info->imguiAlloc, a_info->imguiFree, a_info->imguiAllocatorUserData);
				if (!_state.MarkReady())
					return;
				if (_fallbackCoordination.ConsumeSavedResources())
					resources = TakeFallbackResourcesLocked();
			}
			ReleaseFallbackResources(resources);
			L->info("Dear-Modding UI host is ready; Community Shaders is hosted for this session");
			RetryPendingSwapChain();
			SyncOverlayDemand();
		} catch (...) {
			L->error("The host ready callback failed; Community Shaders has no menu this session");
		}
	}

	void HostClient::OnHostUnavailable(DMUI_UnavailableReason a_reason) noexcept
	{
		GoUnavailable(a_reason, true);
	}

	void HostClient::GoUnavailable(DMUI_UnavailableReason a_reason, bool a_allowFallback) noexcept
	{
		try {
			FallbackResources resources;
			bool changed = false;
			bool bootstrapSeen = false;
			bool startFallback = false;
			{
				const std::scoped_lock lock{ _fallbackMutex };
				bootstrapSeen = _fallbackCoordination.BootstrapSeen();
				changed = a_allowFallback && !bootstrapSeen ?
				              _state.ChooseStandaloneFromRegistered() :
				              _state.MarkUnavailable();
				if (!changed)
					return;

				if (a_allowFallback &&
					_fallbackCoordination.OnStandaloneTransition() ==
						FallbackAction::kStandaloneFromSavedResources) {
					resources = TakeFallbackResourcesLocked();
					startFallback = true;
				} else if (!a_allowFallback && _fallbackCoordination.ConsumeSavedResources()) {
					resources = TakeFallbackResourcesLocked();
				}
				ReleasePendingSwapChainLocked();
			}

			L->warn("Dear-Modding UI host unavailable ({})", DescribeUnavailable(a_reason));
			if (!a_allowFallback) {
				ReleaseFallbackResources(resources);
				L->error("Community Shaders has no menu this session; the host still owns Dear ImGui");
				return;
			}

			if (startFallback)
				StartStandaloneFallback(resources);
			else if (bootstrapSeen)
				L->error("No saved renderer resources; the standalone menu cannot start");
		} catch (...) {
			L->error("The host unavailable callback failed");
		}
	}

	void HostClient::DrawPage(Page& a_page) noexcept
	{
		auto recovery = ImGuiRecoverySnapshot::Capture();
		if (!recovery) {
			L->error("Hosted page '{}' could not capture ImGui state", a_page.descriptor.id);
			return;
		}

		try {
			if (!_state.IsReady())
				return;

			auto& menu = Menu::Get();
			menu.PumpHostedMaintenance();

			switch (a_page.descriptor.kind) {
			case HostPageKind::kHome:
				HomePageRenderer::RenderHomePage();
				break;
			case HostPageKind::kGeneral:
				menu.DrawHostedGeneralSettings();
				break;
			case HostPageKind::kAdvanced:
				menu.DrawAdvancedSettings();
				break;
			case HostPageKind::kPresets:
				menu.DrawPresets();
				break;
			case HostPageKind::kFeature:
				if (a_page.feature)
					FeatureListRenderer::RenderFeatureContent(*a_page.feature);
				break;
			case HostPageKind::kOverlay:
				OverlayRenderer::RenderOverlay();
				break;
			}

			SyncOverlayDemand();
		} catch (...) {
			recovery->Recover();
			L->error("Hosted page '{}' failed", a_page.descriptor.id);
		}
	}

	bool HostClient::OnD3D11Bootstrap(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		IDXGISwapChain* a_swapChain,
		HWND a_window) noexcept
	{
		AttachFinalSwapChain(a_swapChain);

		const std::scoped_lock lock{ _fallbackMutex };
		const auto state = _state.Get();
		if (_fallbackCoordination.ObserveBootstrap(state) == BootstrapAction::kStandaloneNow) {
			_state.ChooseStandalone();
			return true;
		}

		if (state == IntegrationState::kRegisteredWaiting) {
			if (SaveFallbackResources(a_device, a_context, a_swapChain, a_window))
				_fallbackCoordination.MarkResourcesSaved();
		}
		try {
			Menu::Get().AttachHostedResources(a_device, a_context, a_window);
		} catch (...) {
			L->error("Failed to prepare hosted menu state");
		}
		return false;
	}

	bool HostClient::SaveFallbackResources(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		IDXGISwapChain* a_swapChain,
		HWND a_window) noexcept
	{
		if (!a_device || !a_context || !a_swapChain || !a_window)
			return false;

		auto previous = TakeFallbackResourcesLocked();
		ReleaseFallbackResources(previous);
		a_device->AddRef();
		a_context->AddRef();
		a_swapChain->AddRef();
		_fallbackDevice = a_device;
		_fallbackContext = a_context;
		_fallbackSwapChain = a_swapChain;
		_fallbackWindow = a_window;
		return true;
	}

	HostClient::FallbackResources HostClient::TakeFallbackResourcesLocked() noexcept
	{
		FallbackResources resources{
			.device = _fallbackDevice,
			.context = _fallbackContext,
			.swapChain = _fallbackSwapChain,
			.window = _fallbackWindow
		};
		_fallbackDevice = nullptr;
		_fallbackContext = nullptr;
		_fallbackSwapChain = nullptr;
		_fallbackWindow = nullptr;
		return resources;
	}

	void HostClient::ReleaseFallbackResources(FallbackResources& a_resources) noexcept
	{
		if (a_resources.device) {
			a_resources.device->Release();
			a_resources.device = nullptr;
		}
		if (a_resources.context) {
			a_resources.context->Release();
			a_resources.context = nullptr;
		}
		if (a_resources.swapChain) {
			a_resources.swapChain->Release();
			a_resources.swapChain = nullptr;
		}
		a_resources.window = nullptr;
	}

	void HostClient::StartStandaloneFallback(FallbackResources a_resources) noexcept
	{
		if (!a_resources.IsValid()) {
			L->error("No saved renderer resources; the standalone menu cannot start");
			ReleaseFallbackResources(a_resources);
			return;
		}

		try {
			auto& menu = Menu::Get();
			menu.OnD3D11Ready(a_resources.device, a_resources.context, a_resources.window);
			// Chain the host's existing Present detour.
			menu.HookPresentOn(a_resources.swapChain);
			L->info("Standalone menu started from the saved renderer resources");
		} catch (...) {
			L->error("The standalone menu fallback failed to start");
		}
		ReleaseFallbackResources(a_resources);
	}

	void HostClient::AttachFinalSwapChain(IDXGISwapChain* a_swapChain) noexcept
	{
		if (!a_swapChain)
			return;

		SwapChainHandoffAttempt attempt;
		{
			const std::scoped_lock lock{ _fallbackMutex };
			const auto state = _state.Get();
			if (state != IntegrationState::kRegisteredWaiting &&
				state != IntegrationState::kHostedReady)
				return;

			attempt = AttemptSwapChainHandoff(_api, _client, a_swapChain, state);
			if (attempt.action == SwapChainHandoffAction::kRetry)
				RetainPendingSwapChainLocked(a_swapChain);
			else
				ReleasePendingSwapChainLocked();
		}

		switch (attempt.action) {
		case SwapChainHandoffAction::kAccepted:
			L->info("Attached final swapchain to the Dear-Modding UI host");
			break;
		case SwapChainHandoffAction::kRetry:
			L->warn("Dear-Modding UI swapchain handoff deferred: {}",
				DescribeResult(attempt.result));
			break;
		case SwapChainHandoffAction::kFallback:
			FallBackToStandalone(DescribeResult(attempt.result));
			break;
		case SwapChainHandoffAction::kRejectAfterReady:
			L->error("Dear-Modding UI swapchain handoff failed after readiness: {}",
				DescribeResult(attempt.result));
			break;
		}
	}

	void HostClient::RetryPendingSwapChain() noexcept
	{
		IDXGISwapChain* pending = nullptr;
		{
			const std::scoped_lock lock{ _fallbackMutex };
			pending = _pendingHostSwapChain;
			if (pending)
				pending->AddRef();
		}
		if (!pending)
			return;

		AttachFinalSwapChain(pending);
		pending->Release();
	}

	void HostClient::RetainPendingSwapChainLocked(IDXGISwapChain* a_swapChain) noexcept
	{
		if (_pendingHostSwapChain == a_swapChain)
			return;
		ReleasePendingSwapChainLocked();
		a_swapChain->AddRef();
		_pendingHostSwapChain = a_swapChain;
	}

	void HostClient::ReleasePendingSwapChainLocked() noexcept
	{
		if (_pendingHostSwapChain) {
			_pendingHostSwapChain->Release();
			_pendingHostSwapChain = nullptr;
		}
	}

	bool HostClient::IsHostMenuVisible() const noexcept
	{
		if (!_state.IsReady() || !_api || !_api->isMenuVisible)
			return false;
		std::uint32_t visible = 0;
		return _api->isMenuVisible(&visible) == DMUI_RESULT_OK && visible != 0;
	}

	bool HostClient::OverlayWanted() const noexcept
	{
		if (!Menu::Get().IsOverlayVisible())
			return false;
		for (const Feature* feature : _features) {
			try {
				if (feature && feature->IsHealthy() && feature->IsOverlayActive())
					return true;
			} catch (...) {
			}
		}
		return false;
	}

	void HostClient::SyncOverlayDemand() noexcept
	{
		if (!_state.IsReady() || !_api || _client == DMUI_INVALID_CLIENT_HANDLE || !_overlayPage)
			return;

		const std::scoped_lock lock{ _demandMutex };
		const auto action = _overlayDemand.Plan(OverlayWanted());
		if (action == OverlayDemandModel::Action::kNone)
			return;

		const auto result = action == OverlayDemandModel::Action::kRequest ?
		                        _api->requestFrame(_client, _overlayPage->handle) :
		                        _api->releaseFrame(_client, _overlayPage->handle);
		if (result == DMUI_RESULT_OK)
			_overlayDemand.Confirm(action);
		else
			L->warn("Overlay frame demand was rejected: {}", DescribeResult(result));
	}
}
