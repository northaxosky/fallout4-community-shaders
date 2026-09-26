#include "Host/HostClient.h"

#include "Feature.h"
#include "Host/HostClientOptions.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Plugin.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/Hotkey.h"

#include "PerformanceOverlay.h"
#include "RenderDoc.h"

#include <algorithm>
#include <array>
#include <exception>
#include <format>
#include <string>
#include <utility>

#include <d3d11.h>
#include <dxgi.h>

namespace cs::host
{
	namespace
	{
		auto* L = cs::log::Get("cs.host");

		FeaturePageInput DescribeFeature(Feature& a_feature)
		{
			return {
				.name = std::string(a_feature.GetName()),
				.displayName = std::string(a_feature.GetDisplayName()),
				.category = a_feature.GetCategory(),
				.summary = a_feature.GetFeatureSummary(),
				.active = a_feature.IsActive(),
				.installed = a_feature.IsInstalled()
			};
		}

		bool LoadAtBoot(const Feature& a_feature)
		{
			const auto feature = feature_config::GetFeature(a_feature.GetConfigKey());
			return feature &&
				feature->get("load") &&
				feature->get("load")->value_or(false);
		}

	}

	HostClient::HostClient() :
		_client(
			"dearmodding.community-shaders",
			"Community Shaders",
			{ Plugin::VERSION[0], Plugin::VERSION[1] },
			kClientIconName,
			{},
			kClientOptions)
	{}

	HostClient& HostClient::Get()
	{
		static HostClient instance;
		return instance;
	}

	void HostClient::DiscoverAndRegister() noexcept
	{
		try {
			_registrationComplete.store(false, std::memory_order_release);
			if (!_client.Connect()) {
				if (!_client.HostPresent()) {
					L->info(
						"DearModdingUI host not found; Community Shaders is running headless "
						"(no menu, overlay, or diagnostic hotkeys)");
				} else {
					L->warn(
						"DearModdingUI connection rejected: {}; Community Shaders is running "
						"headless (no menu, overlay, or diagnostic hotkeys)",
						DMUI_ResultToString(_client.LastResult()));
				}
				return;
			}
			if (!RegisterPages()) {
				(void)_client.SetStatus(
					DMUI_STATUS_SEVERITY_ERROR,
					"Community Shaders page registration is incomplete; callbacks are disabled.");
				L->error(
					"DearModdingUI page registration was only partially accepted; "
					"registered callbacks remain disabled");
				return;
			}
			if (!RegisterActionsAndObservers() || !RegisterHotkeys()) {
				(void)_client.SetStatus(
					DMUI_STATUS_SEVERITY_ERROR,
					"Community Shaders registration is incomplete; callbacks are disabled.");
				L->error(
					"DearModdingUI registration was only partially accepted; "
					"Community Shaders callbacks remain disabled");
				return;
			}
			_registrationComplete.store(true, std::memory_order_release);
			PublishInitialStatus();
			L->info(
				"Registered {} forwarding-only pages with DearModdingUI",
				_pages.size());
		} catch (const std::exception& error) {
			L->error(
				"DearModdingUI registration failed: {}; registered callbacks remain disabled",
				error.what());
		} catch (...) {
			L->error(
				"DearModdingUI registration failed with a non-standard exception; "
				"registered callbacks remain disabled");
		}
	}

	bool HostClient::RegisterPages()
	{
		std::vector<FeaturePageInput> inputs;
		std::vector<Feature*> features;
		for (auto* feature : FeatureManager::Get().GetRegisteredFeatures()) {
			if (!feature || !feature->IsInMenu())
				continue;
			inputs.push_back(DescribeFeature(*feature));
			features.push_back(feature);
		}

		auto catalog = BuildPageCatalog(inputs);
		for (const auto& category : catalog.categories) {
			if (!_client.AddCategory({
					.id = category.id.c_str(),
					.displayName = category.displayName.c_str(),
					.sortKey = category.sortKey,
					.iconName = category.iconName.c_str() })) {
				LogFailure("register category");
				return false;
			}
		}

		_pages.reserve(catalog.pages.size());
		for (auto descriptor : catalog.pages) {
			auto page = std::make_unique<Page>();
			page->feature = descriptor.kind == HostPageKind::kFeature ?
				features[descriptor.featureIndex] :
				nullptr;
			page->descriptor = std::move(descriptor);
			const dmui::PageDescriptor pageDescriptor{
				.id = page->descriptor.id.c_str(),
				.displayName = page->descriptor.displayName.c_str(),
				.categoryId = page->descriptor.categoryId.c_str(),
				.summary = page->descriptor.summary.c_str(),
				.sortKey = page->descriptor.sortKey,
				.kind = page->descriptor.kind == HostPageKind::kOverlay ?
					DMUI_PAGE_KIND_OVERLAY :
					DMUI_PAGE_KIND_SETTINGS
			};
			_pages.push_back(std::move(page));
			auto* stored = _pages.back().get();
			const auto handle = _client.AddPage(
				pageDescriptor,
				[this, stored] {
					if (_registrationComplete.load(std::memory_order_acquire))
						DrawPage(*stored);
				});
			if (!handle) {
				LogFailure("register page");
				return false;
			}
			stored->handle = *handle;
			if (stored->descriptor.kind == HostPageKind::kOverlay)
				_overlayPage = stored;
		}
		return true;
	}

	bool HostClient::RegisterActionsAndObservers()
	{
		bool succeeded = true;
		if (!_client.AddAction(
				"clear-shader-cache",
				"Clear Shader Cache",
				"trash",
				"Delete compiled shader records after confirmation.",
				[this] {
					if (_registrationComplete.load(std::memory_order_acquire))
						Menu::Get().RequestClearShaderCache();
				},
				100)) {
			LogFailure("register Clear Shader Cache action");
			succeeded = false;
		}
		if (!_client.AddFrameObserver([this] {
				if (_registrationComplete.load(std::memory_order_acquire))
					ObserveFrame();
			})) {
			LogFailure("register frame observer");
			succeeded = false;
		}
		if (!_client.AddPageActivityObserver(
				[this](const dmui::PageActivity& activity) {
					if (_registrationComplete.load(std::memory_order_acquire))
						ObservePageActivity(activity);
				})) {
			LogFailure("register page activity observer");
			succeeded = false;
		}
		return succeeded;
	}

	bool HostClient::RegisterHotkeys()
	{
		bool succeeded = true;
		auto* performance = features::PerformanceOverlay::GetSingleton();
		_overlayHotkey = _client.AddHotkeyAction(
			"dearmodding.cs.performance-overlay.toggle",
			"Toggle Performance Overlay",
			performance->SuggestedToggleHotkey().c_str(),
			[this](bool pressed) {
				auto* feature = features::PerformanceOverlay::GetSingleton();
				if (pressed &&
					_registrationComplete.load(std::memory_order_acquire) &&
					feature->IsHealthy() &&
					feature->IsOverlayActive()) {
					_overlayVisible.store(
						!_overlayVisible.load(std::memory_order_relaxed),
						std::memory_order_relaxed);
				}
			},
			DMUI_HOTKEY_CONTEXT_HOST_INPUT_INACTIVE);
		if (!_overlayHotkey) {
			LogFailure("register performance overlay hotkey");
			succeeded = false;
		} else {
			std::optional<DMUI_Result> failure;
			SetHotkeyEnabled(
				"performance overlay",
				_overlayHotkey,
				performance->IsHealthy() && performance->IsOverlayActive(),
				failure);
			succeeded &= !failure.has_value();
		}

		auto* renderDoc = features::RenderDoc::GetSingleton();
		// Preserve the established policy that multi-frame capture wins when both
		// suggested chords are the same by registering it first.
		_multiCaptureHotkey = _client.AddHotkeyAction(
			"dearmodding.cs.renderdoc.multi-capture",
			"Capture Multiple Frames",
			renderDoc->SuggestedMultiCaptureHotkey().c_str(),
			[this, renderDoc](bool pressed) {
				if (pressed &&
					_registrationComplete.load(std::memory_order_acquire) &&
					renderDoc->IsHealthy() &&
					renderDoc->CaptureHotkeysEnabled())
					renderDoc->TriggerMultiFrameCapture();
			},
			DMUI_HOTKEY_CONTEXT_HOST_INPUT_INACTIVE);
		if (!_multiCaptureHotkey) {
			LogFailure("register RenderDoc multi-capture hotkey");
			succeeded = false;
		} else {
			std::optional<DMUI_Result> failure;
			SetHotkeyEnabled(
				"RenderDoc multi-frame capture",
				_multiCaptureHotkey,
				renderDoc->IsHealthy() && renderDoc->CaptureHotkeysEnabled(),
				failure);
			succeeded &= !failure.has_value();
		}

		_captureHotkey = _client.AddHotkeyAction(
			"dearmodding.cs.renderdoc.capture",
			"Capture One Frame",
			renderDoc->SuggestedCaptureHotkey().c_str(),
			[this, renderDoc](bool pressed) {
				if (pressed &&
					_registrationComplete.load(std::memory_order_acquire) &&
					renderDoc->IsHealthy() &&
					renderDoc->CaptureHotkeysEnabled())
					renderDoc->TriggerCapture();
			},
			DMUI_HOTKEY_CONTEXT_HOST_INPUT_INACTIVE);
		if (!_captureHotkey) {
			LogFailure("register RenderDoc capture hotkey");
			succeeded = false;
		} else {
			std::optional<DMUI_Result> failure;
			SetHotkeyEnabled(
				"RenderDoc single-frame capture",
				_captureHotkey,
				renderDoc->IsHealthy() && renderDoc->CaptureHotkeysEnabled(),
				failure);
			succeeded &= !failure.has_value();
		}

		if (renderDoc->SuggestedCaptureHotkey() ==
			renderDoc->SuggestedMultiCaptureHotkey()) {
			L->warn(
				"RenderDoc capture hotkeys both suggest '{}'; multi-frame capture "
				"takes precedence unless the host override changes either binding",
				renderDoc->SuggestedMultiCaptureHotkey());
		}

		const auto dump = log::GetDumpHotkey().ToString();
		_dumpHotkey = _client.AddHotkeyAction(
			"dearmodding.cs.logging.dump",
			"Dump Community Shaders Log",
			dump.c_str(),
			[this](bool pressed) {
				if (pressed &&
					_registrationComplete.load(std::memory_order_acquire))
					telemetry::pump::RequestDump();
			},
			DMUI_HOTKEY_CONTEXT_ALWAYS);
		if (!_dumpHotkey) {
			LogFailure("register log dump hotkey");
			succeeded = false;
		}
		return succeeded;
	}

	void HostClient::PublishInitialStatus() noexcept
	{
		std::size_t active{};
		std::size_t failed{};
		for (const auto* feature : FeatureManager::Get().GetRegisteredFeatures()) {
			if (!feature)
				continue;
			active += feature->IsActive() ? 1u : 0u;
			if (feature->GetState().runtimeState != FeatureRuntimeState::kFailed)
				continue;
			++failed;
			const auto scope = std::string(feature->GetName());
			const auto detail = feature->GetState().detail.empty() ?
				std::string("Feature initialization failed.") :
				feature->GetState().detail;
			if (!_client.ReportDiagnostic({
					DMUI_STATUS_SEVERITY_ERROR,
					scope.c_str(),
					"Feature unavailable",
					detail.c_str() })) {
				LogFailure("report feature diagnostic");
			}
		}

		const auto status = failed ?
			std::format(
				"{} feature{} active; {} failed. See diagnostics.",
				active,
				active == 1 ? "" : "s",
				failed) :
			std::format(
				"{} feature{} active.",
				active,
				active == 1 ? "" : "s");
		if (!_client.SetStatus(
				failed ? DMUI_STATUS_SEVERITY_WARNING : DMUI_STATUS_SEVERITY_INFO,
				status.c_str())) {
			LogFailure("publish client status");
		}
	}

	void HostClient::DrawPage(Page& a_page)
	{
		switch (a_page.descriptor.kind) {
		case HostPageKind::kHome:
			Menu::Get().DrawHome(_client);
			break;
		case HostPageKind::kAdvanced:
			Menu::Get().DrawAdvanced(_client);
			break;
		case HostPageKind::kPresets:
			Menu::Get().DrawPresets(_client);
			break;
		case HostPageKind::kFeature:
			if (a_page.feature)
				DrawFeaturePage(*a_page.feature);
			break;
		case HostPageKind::kOverlay:
			DrawOverlayPage();
			break;
		}
	}

	void HostClient::DrawFeaturePage(Feature& a_feature)
	{
		const auto reportCallbackFailure = [&](const char* a_phase,
											  const char* a_summary,
											  const char* a_detail) {
			FeatureManager::Get().QuarantineRuntimeCallback(
				a_feature,
				a_phase,
				a_detail);
			FeatureManager::Get().FinishRuntimeCallbackPass();
			if (!_client.ReportDiagnostic({
					DMUI_STATUS_SEVERITY_ERROR,
					a_feature.GetName().data(),
					a_summary,
					a_detail })) {
				LogFailure("report feature callback diagnostic");
			}
		};

		const bool loadAtBoot = LoadAtBoot(a_feature);
		const auto& state = a_feature.GetState();
		dmui::SettingsTableScope table{
			_client,
			std::format("feature-settings-{}", a_feature.GetName()).c_str() };
		if (table.Result() != DMUI_RESULT_OK) {
			LogFailure("begin feature settings table");
			return;
		}
		if (!table.Visible())
			return;

		if (state.runtimeState == FeatureRuntimeState::kFailed ||
			state.runtimeState == FeatureRuntimeState::kDegraded) {
			dmui::SettingsRowScope row{
				_client,
				"feature-failure",
				state.runtimeState == FeatureRuntimeState::kFailed ?
					"Error" :
					"Warning",
				"",
				dmui::RowPresentation::Layout::kFullSpan };
			if (row.Result() != DMUI_RESULT_OK) {
				LogFailure("begin feature failure row");
				return;
			}
			if (row.Visible()) {
				const auto tone =
					state.runtimeState == FeatureRuntimeState::kFailed ?
					dmui::TextTone::kStatusError :
					dmui::TextTone::kStatusWarning;
				if (!dmui::DrawStyledText(
						_client,
						state.detail.empty() ?
							"See the log for details." :
							state.detail,
						{ .tone = tone })) {
					LogFailure("draw feature failure detail");
					return;
				}
				try {
					a_feature.DrawFailLoadMessage();
				} catch (const std::exception& error) {
					reportCallbackFailure(
						"DearModdingUI::DrawFailLoadMessage",
						"Feature failure UI callback failed",
						error.what());
					throw;
				} catch (...) {
					reportCallbackFailure(
						"DearModdingUI::DrawFailLoadMessage",
						"Feature failure UI callback failed",
						"non-standard exception");
					throw;
				}
			}
			if (!row.End()) {
				LogFailure("end feature failure row");
				return;
			}
			if (!table.End())
				LogFailure("end feature settings table");
			return;
		}

		if (!a_feature.IsActive()) {
			dmui::SettingsRowScope row{
				_client,
				"inactive-feature",
				"Availability",
				"",
				dmui::RowPresentation::Layout::kFullSpan };
			if (row.Result() != DMUI_RESULT_OK) {
				LogFailure("begin inactive feature row");
				return;
			}
			if (!row.Visible())
				return;
			if (a_feature.IsInstalled()) {
				if (loadAtBoot) {
					if (!dmui::DrawStyledText(
							_client,
							"This feature will be available after restart.",
							{
								.tone =
									dmui::TextTone::kStatusRestartNeeded
							})) {
						LogFailure("draw feature restart availability");
						return;
					}
				} else {
					if (!dmui::DrawStyledText(
							_client,
							"Not loaded. Enable it in Advanced > Load on startup, then restart.",
							{ .tone = dmui::TextTone::kStatusDisable })) {
						LogFailure("draw feature disabled availability");
						return;
					}
				}
			} else {
				try {
					a_feature.DrawUnloadedUI();
				} catch (const std::exception& error) {
					reportCallbackFailure(
						"DearModdingUI::DrawUnloadedUI",
						"Feature help callback failed",
						error.what());
					throw;
				} catch (...) {
					reportCallbackFailure(
						"DearModdingUI::DrawUnloadedUI",
						"Feature help callback failed",
						"non-standard exception");
					throw;
				}
				std::optional<std::string> modLink;
				try {
					modLink = a_feature.GetFeatureModLink();
				} catch (const std::exception& error) {
					reportCallbackFailure(
						"DearModdingUI::GetFeatureModLink",
						"Feature link callback failed",
						error.what());
					throw;
				} catch (...) {
					reportCallbackFailure(
						"DearModdingUI::GetFeatureModLink",
						"Feature link callback failed",
						"non-standard exception");
					throw;
				}
				if (modLink && !modLink->empty()) {
					const std::array links{
						dmui::Link{
							.label = "Open feature mod page",
							.external = {
								.targetKind = DMUI_EXTERNAL_TARGET_URI,
								.target = modLink->c_str() },
							.note = "Opens the feature download page in your default browser.",
							.action = dmui::LinkAction::kOpenExternal }
					};
					if (!_client.DrawLinkRow("feature-mod-link", links))
						LogFailure("draw feature mod link");
				}
			}
			if (!row.End()) {
				LogFailure("end inactive feature row");
				return;
			}
			if (!table.End())
				LogFailure("end feature settings table");
			return;
		}

		bool restoreDefaults{};
		{
			dmui::SettingsRowScope row{
				_client,
				"feature-controls",
				"Settings",
				"Live feature controls.",
				dmui::RowPresentation::Layout::kFullSpan };
			if (row.Result() != DMUI_RESULT_OK) {
				LogFailure("begin feature controls row");
				return;
			}
			if (row.Visible()) {
				if (!FeatureManager::Get().PrepareMenuCallback(
						a_feature, "DearModdingUI::DrawSettings"))
					return;
				CS_FEATURE_ZONE(&a_feature, "DrawSettings");
				try {
					a_feature.DrawSettings();
					a_feature.FlushSettings(true);
				} catch (const std::exception& error) {
					FeatureManager::Get().QuarantineRuntimeCallback(
						a_feature,
						"DearModdingUI::DrawSettings",
						error.what());
					FeatureManager::Get().FinishRuntimeCallbackPass();
					if (!_client.ReportDiagnostic({
							DMUI_STATUS_SEVERITY_ERROR,
							a_feature.GetName().data(),
							"Feature settings callback failed",
							error.what() })) {
						LogFailure("report feature settings callback diagnostic");
					}
					throw;
				} catch (...) {
					FeatureManager::Get().QuarantineRuntimeCallback(
						a_feature,
						"DearModdingUI::DrawSettings",
						"non-standard exception");
					FeatureManager::Get().FinishRuntimeCallbackPass();
					if (!_client.ReportDiagnostic({
							DMUI_STATUS_SEVERITY_ERROR,
							a_feature.GetName().data(),
							"Feature settings callback failed",
							"non-standard exception" })) {
						LogFailure("report feature settings callback diagnostic");
					}
					throw;
				}
				bool resettable{};
				try {
					resettable = a_feature.HasResettableSettings();
				} catch (const std::exception& error) {
					reportCallbackFailure(
						"DearModdingUI::HasResettableSettings",
						"Feature reset metadata callback failed",
						error.what());
					throw;
				} catch (...) {
					reportCallbackFailure(
						"DearModdingUI::HasResettableSettings",
						"Feature reset metadata callback failed",
						"non-standard exception");
					throw;
				}
				const auto reset = row.End(resettable, resettable);
				if (!reset) {
					LogFailure("end feature controls row");
					return;
				}
				restoreDefaults = *reset;
			}
		}

		if (restoreDefaults) {
			try {
				a_feature.RestoreDefaultSettings();
			} catch (const std::exception& error) {
				reportCallbackFailure(
				"DearModdingUI::RestoreDefaultSettings",
				"Feature reset callback failed",
				error.what());
				throw;
			} catch (...) {
				reportCallbackFailure(
				"DearModdingUI::RestoreDefaultSettings",
				"Feature reset callback failed",
				"non-standard exception");
				throw;
			}
		}

		const auto restartSettings = a_feature.GetRestartSettings();
		for (const auto field : restartSettings) {
			const auto id =
				std::format("restart-required-{}", field);
			const auto label = std::string(field);
			dmui::SettingsRowScope row{
				_client,
				id.c_str(),
				"Restart required",
				"This setting differs from its active startup value.",
				dmui::RowPresentation::Layout::kFullSpan };
			if (row.Result() != DMUI_RESULT_OK) {
				LogFailure("begin restart required row");
				return;
			}
			if (row.Visible()) {
				if (!dmui::DrawStyledText(
						_client,
						label,
						{
							.tone =
								dmui::TextTone::kStatusRestartNeeded
						})) {
					LogFailure("draw restart required setting");
					return;
				}
			}
			if (!row.End()) {
				LogFailure("end restart required row");
				return;
			}
		}
		if (!table.End())
			LogFailure("end feature settings table");
	}

	void HostClient::DrawOverlayPage()
	{
		if (!_overlayVisible.load(std::memory_order_relaxed))
			return;
		auto* performance = features::PerformanceOverlay::GetSingleton();
		if (!FeatureManager::Get().PrepareRuntimeCallback(
				*performance, "DearModdingUI::DrawOverlay"))
			return;
		try {
			performance->DrawOverlay();
		} catch (const std::exception& error) {
			FeatureManager::Get().QuarantineRuntimeCallback(
				*performance,
				"DearModdingUI::DrawOverlay",
				error.what());
			FeatureManager::Get().FinishRuntimeCallbackPass();
			throw;
		} catch (...) {
			FeatureManager::Get().QuarantineRuntimeCallback(
				*performance,
				"DearModdingUI::DrawOverlay",
				"non-standard exception");
			FeatureManager::Get().FinishRuntimeCallbackPass();
			throw;
		}
	}

	void HostClient::ObserveFrame() noexcept
	{
		try {
			const auto state = _client.QueryState();
			if (state && state->state == DMUI_HOST_STATE_READY &&
				!_readyLogged.exchange(true)) {
				L->info(
					"DearModdingUI forwarding backend is ready; hosted pages, overlays, and "
					"hotkeys are active");
			} else if (state && state->state == DMUI_HOST_STATE_UNAVAILABLE &&
				!_unavailableLogged.exchange(true)) {
				L->warn(
					"DearModdingUI became unavailable (reason={}); Community Shaders UI is "
					"disabled for this session",
					state->unavailableReason);
			}

			RetrySwapChain();
			FlushNotification();
			Menu::Get().ObserveHostFrame(_client);

			auto* performance = features::PerformanceOverlay::GetSingleton();
			if (FeatureManager::Get().PrepareRuntimeCallback(
					*performance, "DearModdingUI::TickHostFrame")) {
				try {
					const auto videoMemory = _client.QueryVideoMemory();
					if (videoMemory) {
						_videoMemoryFailure.reset();
					} else {
						LogFailureOnce("video-memory query", _videoMemoryFailure);
					}
					performance->TickHostFrame(
						videoMemory ? videoMemory->used : 0,
						videoMemory ? videoMemory->budget : 0);
				} catch (const std::exception& error) {
					FeatureManager::Get().QuarantineRuntimeCallback(
						*performance,
						"DearModdingUI::TickHostFrame",
						error.what());
					FeatureManager::Get().FinishRuntimeCallbackPass();
				} catch (...) {
					FeatureManager::Get().QuarantineRuntimeCallback(
						*performance,
						"DearModdingUI::TickHostFrame",
						"non-standard exception");
					FeatureManager::Get().FinishRuntimeCallbackPass();
				}
			}
			auto* renderDoc = features::RenderDoc::GetSingleton();
			if (FeatureManager::Get().PrepareRuntimeCallback(
					*renderDoc, "DearModdingUI::TickHostFrame")) {
				try {
					renderDoc->TickHostFrame();
				} catch (const std::exception& error) {
					FeatureManager::Get().QuarantineRuntimeCallback(
						*renderDoc,
						"DearModdingUI::TickHostFrame",
						error.what());
					FeatureManager::Get().FinishRuntimeCallbackPass();
				} catch (...) {
					FeatureManager::Get().QuarantineRuntimeCallback(
						*renderDoc,
						"DearModdingUI::TickHostFrame",
						"non-standard exception");
					FeatureManager::Get().FinishRuntimeCallbackPass();
				}
			}

			SetHotkeyEnabled(
				"performance overlay",
				_overlayHotkey,
				performance->IsHealthy() && performance->IsOverlayActive(),
				_hotkeyEnableFailures[0]);
			SetHotkeyEnabled(
				"RenderDoc single-frame capture",
				_captureHotkey,
				renderDoc->IsHealthy() && renderDoc->CaptureHotkeysEnabled(),
				_hotkeyEnableFailures[1]);
			SetHotkeyEnabled(
				"RenderDoc multi-frame capture",
				_multiCaptureHotkey,
				renderDoc->IsHealthy() && renderDoc->CaptureHotkeysEnabled(),
				_hotkeyEnableFailures[2]);
			ObserveHotkeyBindings();
			SyncOverlay();
		} catch (...) {
			L->error("DearModdingUI frame observer failed");
		}
	}

	void HostClient::ObservePageActivity(
		const dmui::PageActivity& a_activity) noexcept
	{
		if (a_activity.kind != dmui::PageActivityKind::kDeactivated &&
			a_activity.kind != dmui::PageActivityKind::kChanged)
			return;
		Menu::Get().ReleaseDebugImages();

		for (const auto& page : _pages) {
			if (page->handle != a_activity.previousPage || !page->feature)
				continue;
			auto& feature = *page->feature;
			auto& manager = FeatureManager::Get();
			constexpr std::string_view phase = "DearModdingUI::SaveSettings";
			// Inactive features draw editable settings, so they must also flush.
			if (!manager.PrepareMenuCallback(feature, phase))
				return;
			try {
				feature.FlushSettings();
			} catch (const std::exception& error) {
				manager.QuarantineRuntimeCallback(feature, phase, error.what());
				manager.FinishRuntimeCallbackPass();
			} catch (...) {
				manager.QuarantineRuntimeCallback(feature, phase, "non-standard exception");
				manager.FinishRuntimeCallbackPass();
			}
			return;
		}
	}

	void HostClient::SyncOverlay() noexcept
	{
		if (!_overlayPage)
			return;
		auto* performance = features::PerformanceOverlay::GetSingleton();
		const bool wanted =
			_overlayVisible.load(std::memory_order_relaxed) &&
			performance->IsHealthy() &&
			performance->IsOverlayActive();
		const auto demandAction = _overlayFrameDemand.Next(wanted);
		if (demandAction == FrameDemandAction::kRelease) {
			const bool released = _client.ReleaseFrame(_overlayPage->handle);
			_overlayFrameDemand.Complete(demandAction, released);
			if (released)
				_overlayDemandFailure.reset();
			else
				LogFailureOnce(
					"performance overlay frame release",
					_overlayDemandFailure);
			return;
		}
		if (!wanted)
			return;

		const auto options = performance->ManagedOverlayOptions();
		if (_client.ConfigureOverlay(_overlayPage->handle, options))
			_overlayConfigurationFailure.reset();
		else
			LogFailureOnce(
				"performance overlay configuration",
				_overlayConfigurationFailure);
		if (demandAction == FrameDemandAction::kRequest) {
			const bool requested = _client.RequestFrame(_overlayPage->handle);
			_overlayFrameDemand.Complete(demandAction, requested);
			if (requested)
				_overlayDemandFailure.reset();
			else
				LogFailureOnce(
					"performance overlay frame request",
					_overlayDemandFailure);
		}
		if (const auto placement = _client.QueryOverlay(_overlayPage->handle);
			placement && placement->arrangementCompleted) {
			_overlayQueryFailure.reset();
			performance->CommitOverlayPlacement(*placement);
		} else if (placement) {
			_overlayQueryFailure.reset();
		} else {
			LogFailureOnce(
				"performance overlay placement query",
				_overlayQueryFailure);
		}
	}

	void HostClient::ObserveHotkeyBindings() noexcept
	{
		ObserveHotkeyBinding(
			"Performance Overlay",
			_overlayHotkey,
			_hotkeyBindingSnapshots[0]);
		ObserveHotkeyBinding(
			"RenderDoc multi-frame capture",
			_multiCaptureHotkey,
			_hotkeyBindingSnapshots[1]);
		ObserveHotkeyBinding(
			"RenderDoc single-frame capture",
			_captureHotkey,
			_hotkeyBindingSnapshots[2]);
		ObserveHotkeyBinding(
			"telemetry dump",
			_dumpHotkey,
			_hotkeyBindingSnapshots[3]);
	}

	void HostClient::ObserveHotkeyBinding(
		const char* a_name,
		const std::optional<DMUI_HotkeyActionHandle>& a_handle,
		std::string& a_snapshot) noexcept
	{
		if (!a_handle)
			return;
		const auto binding = _client.QueryHotkeyBinding(*a_handle);
		if (!binding) {
			const auto snapshot = std::format(
				"query-error:{}",
				_client.LastResult());
			if (snapshot != a_snapshot) {
				a_snapshot = snapshot;
				LogFailure(std::format("query {} hotkey", a_name));
			}
			return;
		}

		const auto snapshot = std::format(
			"{}:{}",
			binding->state,
			binding->chord);
		if (snapshot == a_snapshot)
			return;
		a_snapshot = snapshot;
		switch (binding->state) {
		case DMUI_HOTKEY_BINDING_BOUND:
			L->info("{} hotkey bound to {}", a_name, binding->chord);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_USER:
			L->info("{} hotkey is unbound by the user", a_name);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_DEFAULT_CONFLICT:
			L->warn("{} hotkey default conflicts with another action", a_name);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_OVERRIDE_CONFLICT:
			L->warn("{} hotkey override conflicts with another action", a_name);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_INVALID_OVERRIDE:
			L->warn("{} hotkey override is invalid", a_name);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_NEVER_SET:
			L->warn("{} hotkey has no binding", a_name);
			break;
		default:
			L->warn("{} hotkey returned unknown binding state {}", a_name, binding->state);
			break;
		}
	}

	void HostClient::SetHotkeyEnabled(
		const char* a_name,
		const std::optional<DMUI_HotkeyActionHandle>& a_handle,
		bool a_enabled,
		std::optional<DMUI_Result>& a_failure) noexcept
	{
		if (!a_handle)
			return;
		if (_client.SetHotkeyActionEnabled(*a_handle, a_enabled)) {
			a_failure.reset();
			return;
		}

		const auto result = _client.LastResult();
		if (!a_failure || *a_failure != result) {
			L->warn(
				"DearModdingUI failed to update {} hotkey enablement: {}",
				a_name,
				DMUI_ResultToString(result));
			a_failure = result;
		}
	}

	void HostClient::OnD3D11Bootstrap(
		ID3D11Device* a_device,
		IDXGISwapChain* a_swapChain,
		HWND a_window) noexcept
	{
		auto* renderDoc = features::RenderDoc::GetSingleton();
		if (renderDoc->IsHealthy()) {
			try {
				renderDoc->BindD3D11CaptureTarget(a_device, a_window);
			} catch (const std::exception& error) {
				FeatureManager::Get().QuarantineRuntimeCallback(
					*renderDoc,
					"DearModdingUI::BindD3D11CaptureTarget",
					error.what());
				FeatureManager::Get().FinishRuntimeCallbackPass();
			} catch (...) {
				FeatureManager::Get().QuarantineRuntimeCallback(
					*renderDoc,
					"DearModdingUI::BindD3D11CaptureTarget",
					"non-standard exception");
				FeatureManager::Get().FinishRuntimeCallbackPass();
			}
		}
		Menu::Get().OnHostDeviceReady();
		if (!_client.IsConnected())
			return;
		if (_client.AttachSwapChain(a_swapChain))
			return;
		if (_client.LastResult() == DMUI_RESULT_RENDERER_BUSY ||
			_client.LastResult() == DMUI_RESULT_HOST_NOT_READY ||
			_client.LastResult() == DMUI_RESULT_HOST_NOT_INITIALIZED) {
			const std::scoped_lock lock{ _swapChainMutex };
			if (_pendingSwapChain)
				_pendingSwapChain->Release();
			_pendingSwapChain = a_swapChain;
			_pendingSwapChain->AddRef();
			L->info("DearModdingUI renderer is busy; final swapchain handoff queued");
			return;
		}
		LogFailure("attach final swapchain");
	}

	void HostClient::RetrySwapChain() noexcept
	{
		IDXGISwapChain* pending{};
		{
			const std::scoped_lock lock{ _swapChainMutex };
			pending = _pendingSwapChain;
			if (pending)
				pending->AddRef();
		}
		if (!pending)
			return;
		const bool attached = _client.AttachSwapChain(pending);
		const auto result = _client.LastResult();
		pending->Release();
		if (!attached &&
			(result == DMUI_RESULT_RENDERER_BUSY ||
				result == DMUI_RESULT_HOST_NOT_READY ||
				result == DMUI_RESULT_HOST_NOT_INITIALIZED))
			return;
		{
			const std::scoped_lock lock{ _swapChainMutex };
			if (_pendingSwapChain) {
				_pendingSwapChain->Release();
				_pendingSwapChain = nullptr;
			}
		}
		if (!attached)
			LogFailure("retry final swapchain handoff");
	}

	void HostClient::PostNotification(
		DMUI_StatusSeverity a_severity,
		std::string a_message,
		std::uint32_t a_durationMilliseconds) noexcept
	{
		if (!_client.IsConnected()) {
			if (a_severity == DMUI_STATUS_SEVERITY_ERROR) {
				L->error(
					"DearModdingUI notification unavailable before host readiness/headless: {}",
					a_message);
			} else if (a_severity == DMUI_STATUS_SEVERITY_WARNING) {
				L->warn(
					"DearModdingUI notification unavailable before host readiness/headless: {}",
					a_message);
			} else {
				L->info(
					"DearModdingUI notification unavailable before host readiness/headless: {}",
					a_message);
			}
			return;
		}
		if (a_message.size() > 1024)
			a_message.resize(1024);
		const std::scoped_lock lock{ _notificationMutex };
		_pendingNotification = PendingNotification{
			a_severity,
			std::move(a_message),
			std::clamp(a_durationMilliseconds, 250u, 30000u)
		};
	}

	void HostClient::FlushNotification() noexcept
	{
		std::optional<PendingNotification> notification;
		{
			const std::scoped_lock lock{ _notificationMutex };
			notification = std::move(_pendingNotification);
			_pendingNotification.reset();
		}
		if (!notification)
			return;
		if (!_client.PostNotification(
				notification->severity,
				notification->message.c_str(),
				notification->durationMilliseconds)) {
			LogFailure("post notification");
		}
	}

	bool HostClient::DrawAnnotatedPlot(
		const char* a_id,
		const DMUI_AnnotatedPlotDescriptor& a_descriptor) noexcept
	{
		if (_client.DrawAnnotatedPlot(a_id, a_descriptor)) {
			_annotatedPlotFailure.reset();
			return true;
		}
		LogFailureOnce("annotated plot draw", _annotatedPlotFailure);
		return false;
	}

	void HostClient::LogFailure(std::string_view a_operation) const noexcept
	{
		L->warn(
			"DearModdingUI {} failed: {}",
			a_operation,
			DMUI_ResultToString(_client.LastResult()));
	}

	void HostClient::LogFailureOnce(
		std::string_view a_operation,
		std::optional<DMUI_Result>& a_lastResult) const noexcept
	{
		const auto result = _client.LastResult();
		if (a_lastResult && *a_lastResult == result)
			return;
		L->warn(
			"DearModdingUI {} failed: {}",
			a_operation,
			DMUI_ResultToString(result));
		a_lastResult = result;
	}
}
