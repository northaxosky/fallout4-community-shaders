#include "Host/HostClient.h"

#include "Feature.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/ProfilingRenderer.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Menu/SettingsLayout.h"
#include "Plugin.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

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
		constexpr std::uint32_t kBindingPollFrames = 120;

		constexpr dmui::ClientOptions kClientOptions{
			.capabilities = DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT
		};

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

		void DrawEnabledToggle(Feature& a_feature)
		{
			if (!a_feature.SupportsEnableToggle())
				return;
			settings::SettingsEdit edit{ a_feature };
			bool enabled = a_feature.IsEnabled();
			if (edit.Discrete(dmui::ui::Checkbox("Enabled", &enabled)))
				a_feature.SetEnabled(enabled);
		}
	}

	HostClient::FrameDemandAction HostClient::FrameDemandTracker::Next(bool a_wanted) const noexcept
	{
		if (a_wanted == _requested)
			return FrameDemandAction::kNone;
		return a_wanted ? FrameDemandAction::kRequest : FrameDemandAction::kRelease;
	}

	void HostClient::FrameDemandTracker::Complete(FrameDemandAction a_action, bool a_succeeded) noexcept
	{
		if (!a_succeeded)
			return;
		if (a_action == FrameDemandAction::kRequest)
			_requested = true;
		else if (a_action == FrameDemandAction::kRelease)
			_requested = false;
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
					"Community Shaders menu unavailable. Restart and check FO4CommunityShaders.log.");
				L->error(
					"DearModdingUI page registration was only partially accepted; "
					"registered callbacks remain disabled");
				return;
			}
			if (!RegisterActionsAndObservers() || !RegisterHotkeys()) {
				(void)_client.SetStatus(
					DMUI_STATUS_SEVERITY_ERROR,
					"Community Shaders menu unavailable. Restart and check FO4CommunityShaders.log.");
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
			if (!_client.AddCategory({ .id = category.id.c_str(),
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
				.kind = page->descriptor.kind == HostPageKind::kOverlay ||
				                page->descriptor.kind == HostPageKind::kShaderCompileOverlay ?
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
				_performanceOverlay.page = stored;
			else if (stored->descriptor.kind == HostPageKind::kShaderCompileOverlay)
				_shaderCompileOverlay.page = stored;
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
				"",
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

	bool HostClient::RegisterHotkey(
		HotkeyChannel& a_channel,
		const char* a_id,
		const char* a_displayName,
		const char* a_suggestedChord,
		std::function<void(bool)> a_callback,
		std::function<bool()> a_enabled,
		DMUI_HotkeyContextPolicy a_contextPolicy)
	{
		a_channel.enabled = std::move(a_enabled);
		a_channel.handle = _client.AddHotkeyAction(
			a_id, a_displayName, a_suggestedChord, std::move(a_callback), a_contextPolicy);
		if (!a_channel.handle) {
			LogFailure(std::format("register {} hotkey", a_channel.name));
			return false;
		}
		SetHotkeyEnabled(a_channel);
		return !a_channel.enableFailure;
	}

	bool HostClient::RegisterHotkeys()
	{
		bool succeeded = true;
		auto* performance = features::PerformanceOverlay::GetSingleton();
		succeeded &= RegisterHotkey(_hotkeys[kOverlay], "dearmodding.cs.performance-overlay.toggle", "Toggle Performance Overlay", performance->SuggestedToggleHotkey().c_str(), [this](bool pressed) {
				auto* feature = features::PerformanceOverlay::GetSingleton();
				if (pressed &&
					_registrationComplete.load(std::memory_order_acquire) &&
					feature->IsHealthy() &&
					feature->IsOverlayActive()) {
					_overlayVisible.store(
						!_overlayVisible.load(std::memory_order_relaxed),
						std::memory_order_relaxed);
				} }, [performance] { return performance->IsHealthy() && performance->IsOverlayActive(); });

		auto* renderDoc = features::RenderDoc::GetSingleton();
		// Multi-frame capture wins conflicting defaults by registering first.
		succeeded &= RegisterHotkey(_hotkeys[kMultiCapture], "dearmodding.cs.renderdoc.multi-capture", "Capture Configured Frames (alternate)", renderDoc->SuggestedMultiCaptureHotkey().c_str(), [this, renderDoc](bool pressed) {
				if (pressed &&
					_registrationComplete.load(std::memory_order_acquire) &&
					renderDoc->IsHealthy() &&
					renderDoc->CaptureHotkeysEnabled())
					renderDoc->TriggerMultiFrameCapture(); }, [renderDoc] { return renderDoc->IsHealthy() && renderDoc->CaptureHotkeysEnabled(); });

		succeeded &= RegisterHotkey(_hotkeys[kCapture], "dearmodding.cs.renderdoc.capture", "Capture Configured Frames", renderDoc->SuggestedCaptureHotkey().c_str(), [this, renderDoc](bool pressed) {
				if (pressed &&
					_registrationComplete.load(std::memory_order_acquire) &&
					renderDoc->IsHealthy() &&
					renderDoc->CaptureHotkeysEnabled())
					renderDoc->TriggerCapture(); }, [renderDoc] { return renderDoc->IsHealthy() && renderDoc->CaptureHotkeysEnabled(); });

		if (renderDoc->SuggestedCaptureHotkey() ==
			renderDoc->SuggestedMultiCaptureHotkey()) {
			L->warn(
				"RenderDoc capture hotkeys both suggest '{}'; multi-frame capture "
				"takes precedence unless the host override changes either binding",
				renderDoc->SuggestedMultiCaptureHotkey());
		}

		succeeded &= RegisterHotkey(_hotkeys[kDump], "dearmodding.cs.logging.dump", "Dump Community Shaders Log", log::GetDumpHotkey().c_str(), [this](bool pressed) {
				if (pressed &&
					_registrationComplete.load(std::memory_order_acquire))
					telemetry::pump::RequestDump(); }, [] { return true; }, DMUI_HOTKEY_CONTEXT_ALWAYS);
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
			if (!_client.ReportDiagnostic({ DMUI_STATUS_SEVERITY_ERROR,
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
		case HostPageKind::kProfiling:
			ProfilingRenderer::RenderStatistics();
			break;
		case HostPageKind::kPresets:
			Menu::Get().DrawPresets(_client);
			break;
		case HostPageKind::kChangelog:
			Menu::Get().DrawChangelog(_client);
			break;
		case HostPageKind::kFeature:
			if (a_page.feature)
				DrawFeaturePage(*a_page.feature);
			break;
		case HostPageKind::kOverlay:
			DrawOverlayPage();
			break;
		case HostPageKind::kShaderCompileOverlay:
			DrawShaderCompileOverlayPage();
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
			if (!_client.ReportDiagnostic({ DMUI_STATUS_SEVERITY_ERROR,
					a_feature.GetName().data(),
					a_summary,
					a_detail })) {
				LogFailure("report feature callback diagnostic");
			}
		};

		const bool loadAtBoot = LoadAtBoot(a_feature);
		const auto& state = a_feature.GetState();
		if (!ui::DrawTable(_client, std::format("feature-settings-{}", a_feature.GetName()).c_str(), [&] {
				if (state.runtimeState == FeatureRuntimeState::kFailed ||
					state.runtimeState == FeatureRuntimeState::kDegraded) {
					return ui::DrawRow(_client, "feature-failure", state.runtimeState == FeatureRuntimeState::kFailed ? "Error" : "Warning", "", dmui::RowPresentation::Layout::kFullSpan, [&](auto&) -> bool {
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
					return false;
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
				return true; });
				}

				if (!a_feature.IsActive()) {
					return ui::DrawRow(_client, "inactive-feature", "Availability", "", dmui::RowPresentation::Layout::kFullSpan, [&](auto&) -> bool {
			if (a_feature.IsInstalled()) {
				if (loadAtBoot) {
					if (!dmui::DrawStyledText(
							_client,
							"This feature will be available after restart.",
							{ .tone =
									dmui::TextTone::kStatusRestartNeeded })) {
						LogFailure("draw feature restart availability");
						return false;
					}
				} else {
					if (!dmui::DrawStyledText(
							_client,
							"Not loaded. Enable it in Advanced > Load on startup, then restart.",
							{ .tone = dmui::TextTone::kStatusDisable })) {
						LogFailure("draw feature disabled availability");
						return false;
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
							.action = dmui::LinkAction::kOpenExternal }
					};
					if (!_client.DrawLinkRow("feature-mod-link", links))
						LogFailure("draw feature mod link");
				}
			}
			return true; });
				}

				bool restoreDefaults{};
				{
					if (!ui::DrawRow(_client, "feature-controls", "", "", dmui::RowPresentation::Layout::kFullSpan, [&](auto& row) -> bool {
				if (!FeatureManager::Get().PrepareMenuCallback(
						a_feature, "DearModdingUI::DrawSettings"))
					return false;
				CS_FEATURE_ZONE(&a_feature, "DrawSettings");
				const bool comparing = settings::liveComparisonActive && a_feature.GetLiveSettingsAccess().snapshot;
				try {
					{
						const dmui::DisabledScope disabled{ comparing };
						DrawEnabledToggle(a_feature);
						a_feature.DrawSettings();
					}
					const std::string name{ a_feature.GetName() };
					if (ProfilingRenderer::HasFeatureTimers(name)) {
						// FO4: a host section stands in for ImGui::SeparatorText.
						if (const ui::Section section{ "feature-profiling", "Profiling" }; section)
							ProfilingRenderer::RenderFeatureTimers(name);
					}
					a_feature.FlushSettings(true);
				} catch (const std::exception& error) {
					FeatureManager::Get().QuarantineRuntimeCallback(
						a_feature,
						"DearModdingUI::DrawSettings",
						error.what());
					if (!_client.ReportDiagnostic({ DMUI_STATUS_SEVERITY_ERROR,
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
					if (!_client.ReportDiagnostic({ DMUI_STATUS_SEVERITY_ERROR,
							a_feature.GetName().data(),
							"Feature settings callback failed",
							"non-standard exception" })) {
						LogFailure("report feature settings callback diagnostic");
					}
					throw;
				}
				bool resettable{};
				bool modified{};
				try {
					resettable = a_feature.HasResettableSettings();
					modified = resettable && a_feature.HasModifiedSettings();
				} catch (const std::exception& error) {
					reportCallbackFailure(
						"DearModdingUI::SettingsRowMetadata",
						"Feature reset metadata callback failed",
						error.what());
					throw;
				} catch (...) {
					reportCallbackFailure(
						"DearModdingUI::SettingsRowMetadata",
						"Feature reset metadata callback failed",
						"non-standard exception");
					throw;
				}
				const auto reset = row.End(resettable && !comparing, modified && !comparing);
				if (!reset) {
					LogFailure("end feature controls row");
					return false;
				}
				restoreDefaults = *reset;
				return true; }))
						return false;
				}

				if (restoreDefaults) {
					try {
						a_feature.RestoreDefaults();
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
					if (!ui::DrawRow(_client, id.c_str(), "Restart required", "", dmui::RowPresentation::Layout::kFullSpan, [&](auto&) -> bool { return dmui::DrawStyledText(_client, label, { .tone = dmui::TextTone::kStatusRestartNeeded }); })) {
						LogFailure("draw restart required row");
						return false;
					}
				}
				return true;
			}))
			LogFailure("draw feature settings table");
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
			throw;
		} catch (...) {
			FeatureManager::Get().QuarantineRuntimeCallback(
				*performance,
				"DearModdingUI::DrawOverlay",
				"non-standard exception");
			throw;
		}
	}

	void HostClient::DrawShaderCompileOverlayPage()
	{
		_shaderCompile.Draw();
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
			Menu::Get().ObserveHostFrame(_client);

			auto* performance = features::PerformanceOverlay::GetSingleton();
			FeatureManager::Get().RunRuntimeCallback(*performance, "DearModdingUI::TickHostFrame", [&] {
				// Memory is shown by the visible overlay and in telemetry only.
				const bool wantsMemory = telemetry::pump::Enabled() ||
				                         (_overlayVisible.load(std::memory_order_relaxed) && performance->IsHealthy() && performance->IsOverlayActive());
				decltype(_client.QueryVideoMemory()) videoMemory;
				if (wantsMemory)
					videoMemory = _client.QueryVideoMemory();
				if (!wantsMemory) {
					_videoMemoryFailure.reset();
				} else if (videoMemory) {
					_videoMemoryFailure.reset();
				} else {
					LogFailureOnce("video-memory query", _videoMemoryFailure);
				}
				performance->TickHostFrame(
					videoMemory ? videoMemory->used : 0,
					videoMemory ? videoMemory->budget : 0);
			});
			for (auto& channel : _hotkeys)
				SetHotkeyEnabled(channel);
			// Bindings change only through host UI and are only logged.
			if (_bindingPollCountdown == 0) {
				_bindingPollCountdown = kBindingPollFrames;
				ObserveHotkeyBindings();
			} else {
				--_bindingPollCountdown;
			}
			SyncPerformanceOverlay();
			SyncShaderCompileOverlay();
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
			} catch (...) {
				manager.QuarantineRuntimeCallback(feature, phase, "non-standard exception");
			}
			return;
		}
	}

	void HostClient::SyncPerformanceOverlay() noexcept
	{
		auto* performance = features::PerformanceOverlay::GetSingleton();
		const bool wanted =
			_overlayVisible.load(std::memory_order_relaxed) &&
			performance->IsHealthy() &&
			performance->IsOverlayActive();
		SyncOverlay(
			_performanceOverlay,
			"performance overlay",
			wanted,
			performance->ManagedOverlayOptions());
	}

	void HostClient::SyncShaderCompileOverlay() noexcept
	{
		SyncOverlay(
			_shaderCompileOverlay,
			"shader compilation overlay",
			_shaderCompile.Observe(),
			ShaderCompileOverlay::ManagedOverlayOptions());
	}

	void HostClient::SyncOverlay(
		OverlayChannel& a_overlay,
		std::string_view a_name,
		bool a_wanted,
		const DMUI_ManagedOverlayOptions& a_options) noexcept
	{
		if (!a_overlay.page)
			return;
		const auto demandAction = a_overlay.demand.Next(a_wanted);
		if (demandAction == FrameDemandAction::kRelease) {
			const bool released = _client.ReleaseFrame(a_overlay.page->handle);
			a_overlay.demand.Complete(demandAction, released);
			if (released)
				a_overlay.demandFailure.reset();
			else
				LogFailureOnce(
					std::format("{} frame release", a_name),
					a_overlay.demandFailure);
			return;
		}
		if (!a_wanted)
			return;

		// Hosts reject options their ABI lacks; never show an unconfigured overlay.
		if (!_client.ConfigureOverlay(a_overlay.page->handle, a_options)) {
			LogFailureOnce(
				std::format("{} configuration", a_name),
				a_overlay.configurationFailure);
			return;
		}
		a_overlay.configurationFailure.reset();
		if (demandAction == FrameDemandAction::kRequest) {
			const bool requested = _client.RequestFrame(a_overlay.page->handle);
			a_overlay.demand.Complete(demandAction, requested);
			if (requested)
				a_overlay.demandFailure.reset();
			else
				LogFailureOnce(
					std::format("{} frame request", a_name),
					a_overlay.demandFailure);
		}
	}

	void HostClient::ObserveHotkeyBindings() noexcept
	{
		for (auto& channel : _hotkeys)
			ObserveHotkeyBinding(channel);
	}

	void HostClient::ObserveHotkeyBinding(
		HotkeyChannel& a_channel) noexcept
	{
		if (!a_channel.handle)
			return;
		const auto binding = _client.QueryHotkeyBinding(*a_channel.handle);
		if (!binding) {
			const auto snapshot = std::format(
				"query-error:{}",
				_client.LastResult());
			if (snapshot != a_channel.bindingSnapshot) {
				a_channel.bindingSnapshot = snapshot;
				LogFailure(std::format("query {} hotkey", a_channel.name));
			}
			return;
		}

		const auto snapshot = std::format(
			"{}:{}",
			binding->state,
			binding->chord);
		if (snapshot == a_channel.bindingSnapshot)
			return;
		a_channel.bindingSnapshot = snapshot;
		switch (binding->state) {
		case DMUI_HOTKEY_BINDING_BOUND:
			L->info("{} hotkey bound to {}", a_channel.name, binding->chord);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_USER:
			L->info("{} hotkey is unbound by the user", a_channel.name);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_DEFAULT_CONFLICT:
			L->warn("{} hotkey default conflicts with another action", a_channel.name);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_OVERRIDE_CONFLICT:
			L->warn("{} hotkey override conflicts with another action", a_channel.name);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_INVALID_OVERRIDE:
			L->warn("{} hotkey override is invalid", a_channel.name);
			break;
		case DMUI_HOTKEY_BINDING_UNBOUND_NEVER_SET:
			L->warn("{} hotkey has no binding", a_channel.name);
			break;
		default:
			L->warn("{} hotkey returned unknown binding state {}", a_channel.name, binding->state);
			break;
		}
	}

	void HostClient::SetHotkeyEnabled(HotkeyChannel& a_channel) noexcept
	{
		if (!a_channel.handle)
			return;
		const bool wanted = a_channel.enabled();
		if (a_channel.appliedEnabled == wanted)
			return;
		if (_client.SetHotkeyActionEnabled(*a_channel.handle, wanted)) {
			a_channel.enableFailure.reset();
			a_channel.appliedEnabled = wanted;
			return;
		}

		const auto result = _client.LastResult();
		if (!a_channel.enableFailure || *a_channel.enableFailure != result) {
			L->warn(
				"DearModdingUI failed to update {} hotkey enablement: {}",
				a_channel.name,
				DMUI_ResultToString(result));
			a_channel.enableFailure = result;
		}
	}

	void HostClient::OnD3D11Bootstrap(
		ID3D11Device* a_device,
		IDXGISwapChain* a_swapChain,
		HWND a_window) noexcept
	{
		auto* renderDoc = features::RenderDoc::GetSingleton();
		FeatureManager::Get().RunRuntimeCallback(*renderDoc, "DearModdingUI::BindD3D11CaptureTarget", [&] {
			renderDoc->BindD3D11CaptureTarget(a_device, a_window);
		});
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
		std::uint32_t a_durationMilliseconds,
		std::string a_title) noexcept
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
		if (!_client.PostNotification(
				a_severity,
				a_message.c_str(),
				a_durationMilliseconds,
				a_title.empty() ? "Community Shaders" : a_title.c_str())) {
			LogFailure("post notification");
		}
	}

	void HostClient::ResetOverlay() noexcept
	{
		if (_performanceOverlay.page && !_client.ResetOverlay(_performanceOverlay.page->handle))
			LogFailure("reset performance overlay layout");
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
