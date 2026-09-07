#include "Menu/Menu.h"

#include "Feature.h"
#include "Host/HostClient.h"
#include "Log.h"
#include "Plugin.h"
#include "Render/Engine.h"
#include "Settings/FeatureConfig.h"
#include "Settings/PresetManager.h"
#include "Telemetry/Telemetry.h"
#include "Utils/PhysicalFile.h"
#include "Utils/ShaderCache/CacheStorage.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <ranges>
#include <span>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>

namespace
{
	auto* L = cs::log::Get("cs.menu");

	constexpr std::string_view kPresetRoot =
		"Data\\F4SE\\Plugins\\FO4CommunityShaders\\Presets";
	constexpr std::uint64_t kImageRetryFrames = 60;

	void OpenFileLocation(const std::filesystem::path& a_file)
	{
		const auto resolved = cs::files::PhysicalFilePath(a_file);
		if (!resolved) {
			L->warn(
				"Cannot resolve physical location of '{}': {}",
				a_file.string(), resolved.error().message());
			cs::Menu::ShowToast(
				"Could not locate the backing file. Use MO2's virtual folder browser; see log.",
				4.0,
				DMUI_STATUS_SEVERITY_ERROR);
			return;
		}
		const auto folder = resolved->parent_path();
		const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
			nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
		if (result <= 32) {
			L->warn("Cannot open folder '{}': shell error {}", folder.string(), result);
			cs::Menu::ShowToast("Could not open the folder; see log.", 4.0, DMUI_STATUS_SEVERITY_ERROR);
		}
	}

	ImVec4 ThemeColor(
		dmui::Client& a_client,
		DMUI_StatusSeverity a_severity)
	{
		if (const auto colors = a_client.GetThemeColors()) {
			switch (a_severity) {
			case DMUI_STATUS_SEVERITY_SUCCESS:
				return cs::ui::ToImVec4(colors->statusSuccess);
			case DMUI_STATUS_SEVERITY_WARNING:
				return cs::ui::ToImVec4(colors->statusWarning);
			case DMUI_STATUS_SEVERITY_ERROR:
				return cs::ui::ToImVec4(colors->statusError);
			default:
				return cs::ui::ToImVec4(colors->statusInfo);
			}
		}
		return ImGui::GetStyleColorVec4(ImGuiCol_Text);
	}

	std::string AdapterDescription()
	{
		auto* device = cs::engine::GetDevice();
		if (!device)
			return "D3D11 device not ready";

		IDXGIDevice* dxgiDevice{};
		if (FAILED(device->QueryInterface(
				__uuidof(IDXGIDevice),
				reinterpret_cast<void**>(&dxgiDevice)))) {
			return "adapter query unavailable";
		}
		IDXGIAdapter* adapter{};
		const auto adapterResult = dxgiDevice->GetAdapter(&adapter);
		dxgiDevice->Release();
		if (FAILED(adapterResult) || !adapter)
			return "adapter query unavailable";

		DXGI_ADAPTER_DESC description{};
		const auto descriptionResult = adapter->GetDesc(&description);
		adapter->Release();
		if (FAILED(descriptionResult))
			return "adapter description unavailable";

		const auto length = std::char_traits<wchar_t>::length(description.Description);
		if (length == 0)
			return "unnamed adapter";
		const auto bytes = WideCharToMultiByte(
			CP_UTF8,
			0,
			description.Description,
			static_cast<int>(length),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (bytes <= 0)
			return "adapter description unavailable";
		std::string result(static_cast<std::size_t>(bytes), '\0');
		(void)WideCharToMultiByte(
			CP_UTF8,
			0,
			description.Description,
			static_cast<int>(length),
			result.data(),
			bytes,
			nullptr,
			nullptr);
		return result;
	}

	template <std::size_t N>
	bool DrawChoice(
		const char* a_id,
		int& a_selected,
		const std::array<const char*, N>& a_labels)
	{
		if (a_selected < 0 || static_cast<std::size_t>(a_selected) >= N)
			a_selected = 0;
		bool changed{};
		if (ImGui::BeginCombo(a_id, a_labels[static_cast<std::size_t>(a_selected)])) {
			for (std::size_t index = 0; index < N; ++index) {
				if (ImGui::Selectable(
						a_labels[index],
						static_cast<int>(index) == a_selected)) {
					a_selected = static_cast<int>(index);
					changed = true;
				}
				if (static_cast<int>(index) == a_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	const cs::FeatureDebugView* ResolveDebugView(
		std::string_view a_feature,
		std::string_view a_view)
	{
		for (const auto* feature : cs::FeatureManager::Get().GetAll()) {
			if (!feature || feature->GetName() != a_feature)
				continue;
			const auto views = feature->GetDebugViews();
			const auto found = std::ranges::find(views, a_view, &cs::FeatureDebugView::id);
			return found == views.end() ? nullptr : &*found;
		}
		return nullptr;
	}
}

namespace cs
{
	Menu& Menu::Get()
	{
		static Menu instance;
		return instance;
	}

	void Menu::Load()
	{
		_debugViews.Clear();
		_legacyOverlayToggleHotkey.clear();
		const auto root = feature_config::GetMergedRoot();
		_startupLoads.Capture(root);
		const auto* menu = root["menu"].as_table();
		if (!menu)
			return;

		const auto legacy =
			host::ParseLegacyKeyboardHotkey(menu->get("overlay_toggle_key"));
		if (legacy.present && legacy.valid) {
			_legacyOverlayToggleHotkey = legacy.chord;
		} else if (legacy.present) {
			L->warn(
				"Ignoring legacy menu.overlay_toggle_key: {}",
				legacy.error);
		}

		std::string fullscreenFeature;
		std::string fullscreenView;
		if (const auto value = (*menu)["debug_view_feature"].value<std::string>())
			fullscreenFeature = *value;
		if (const auto value = (*menu)["debug_view"].value<std::string>())
			fullscreenView = *value;
		if (const auto* previews = (*menu)["debug_view_previews"].as_table()) {
			for (const auto& [feature, node] : *previews) {
				if (const auto view = node.value<std::string>()) {
					_debugViews.Select(
						std::string(feature.str()),
						*view,
						FeatureDebugViewKind::kTexturePreview);
				} else {
					L->warn("menu.debug_view_previews.{} must be a string", feature.str());
				}
			}
		}
		if (!fullscreenFeature.empty() && !fullscreenView.empty()) {
			_debugViews.Select(
				std::move(fullscreenFeature),
				std::move(fullscreenView),
				FeatureDebugViewKind::kFullscreen);
		}
	}

	bool Menu::Save()
	{
		toml::table menu;
		const auto root = feature_config::GetMergedRoot();
		if (const auto* existing = root["menu"].as_table())
			menu = *existing;

		const auto& fullscreen = _debugViews.Fullscreen();
		menu.insert_or_assign("debug_view_feature", fullscreen.feature);
		menu.insert_or_assign("debug_view", fullscreen.view);
		toml::table previews;
		for (const auto& [feature, view] : _debugViews.Previews())
			previews.insert_or_assign(feature, view);
		menu.insert_or_assign("debug_view_previews", std::move(previews));

		const auto result = feature_config::UpdateTopLevelSection("menu", menu);
		if (!result) {
			L->warn("Failed to save debug-view configuration: {}", result.error);
			return false;
		}
		return true;
	}

	void Menu::ApplyDebugViewSelections()
	{
		debug_view::SelectionState valid;
		const auto& fullscreen = _debugViews.Fullscreen();
		if (!fullscreen.Empty()) {
			const auto* view = ResolveDebugView(fullscreen.feature, fullscreen.view);
			if (view && view->kind == FeatureDebugViewKind::kFullscreen) {
				valid.Select(
					fullscreen.feature,
					fullscreen.view,
					FeatureDebugViewKind::kFullscreen);
			}
		}
		for (const auto& [feature, viewId] : _debugViews.Previews()) {
			const auto* view = ResolveDebugView(feature, viewId);
			if (view && view->kind == FeatureDebugViewKind::kTexturePreview)
				valid.Select(feature, viewId, FeatureDebugViewKind::kTexturePreview);
		}

		_debugViews = std::move(valid);
		std::vector<FeatureDebugSelection> selections;
		if (!_debugViews.Fullscreen().Empty()) {
			selections.push_back({
				.feature = _debugViews.Fullscreen().feature,
				.view = _debugViews.Fullscreen().view });
		}
		for (const auto& [feature, view] : _debugViews.Previews())
			selections.push_back({ .feature = feature, .view = view });
		if (!FeatureManager::Get().ApplyDebugViews(selections)) {
			L->warn("Invalid debug-view state; disabling debug views");
			_debugViews.Clear();
			FeatureManager::Get().ApplyDebugViews({});
		}
	}

	void Menu::SetDebugViewSelection(const Feature& a_feature, std::string_view a_view)
	{
		_debugImages.erase(std::string(a_feature.GetName()));
		if (a_view.empty()) {
			_debugViews.ClearFeature(a_feature.GetName());
		} else {
			const auto views = a_feature.GetDebugViews();
			const auto selected = std::ranges::find(views, a_view, &FeatureDebugView::id);
			if (selected == views.end())
				return;
			_debugViews.Select(
				std::string(a_feature.GetName()),
				std::string(a_view),
				selected->kind);
		}
		ApplyDebugViewSelections();
		(void)Save();
	}

	void Menu::DrawDebugViewSelector(const Feature& a_feature)
	{
		const auto views = a_feature.GetDebugViews();
		if (views.empty())
			return;

		const auto selectedId = _debugViews.SelectedView(a_feature.GetName());
		const auto selected = std::ranges::find(views, selectedId, &FeatureDebugView::id);
		const std::string preview =
			selected == views.end() ? "Off" : std::string(selected->label);
		if (ImGui::BeginCombo("Debug visualization", preview.c_str())) {
			if (ImGui::Selectable("Off", selected == views.end()))
				SetDebugViewSelection(a_feature, {});
			for (const auto& view : views) {
				const auto label = std::string(view.label);
				if (ImGui::Selectable(label.c_str(), selectedId == view.id))
					SetDebugViewSelection(a_feature, view.id);
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(
				"%s",
				"Fullscreen views are exclusive. Texture previews are independent.");
		}
		DrawDebugTexture(host::HostClient::Get().Client(), a_feature);
	}

	void Menu::DrawDebugTexture(dmui::Client& a_client, const Feature& a_feature)
	{
		const auto views = a_feature.GetDebugViews();
		const auto selectedId = _debugViews.SelectedView(a_feature.GetName());
		const auto selected = std::ranges::find(views, selectedId, &FeatureDebugView::id);
		if (selected == views.end() ||
			selected->kind != FeatureDebugViewKind::kTexturePreview ||
			!selected->textureProvider)
			return;

		const auto texture = selected->textureProvider(a_feature);
		if (!texture.texture || texture.width == 0 || texture.height == 0) {
			ImGui::TextDisabled(
				"%.*s",
				static_cast<int>(texture.unavailableText.size()),
				texture.unavailableText.data());
			_debugImages.erase(std::string(a_feature.GetName()));
			return;
		}
		if (!texture.caption.empty())
			ImGui::TextDisabled("%s", texture.caption.c_str());

		auto& cached = _debugImages[std::string(a_feature.GetName())];
		const bool generationChanged =
			cached.generation != _debugImageGeneration;
		const bool sourceChanged =
			generationChanged ||
			cached.source != texture.texture ||
			cached.width != texture.width ||
			cached.height != texture.height;
		std::optional<DMUI_ImageInfo> imageInfo;
		bool querySucceeded = true;
		if (cached.image.Handle() && !sourceChanged) {
			if (_hostFrameSerial < cached.retryAfterFrame) {
				ImGui::TextDisabled("The host image service is temporarily unavailable.");
				return;
			}
			imageInfo = a_client.QueryImage(cached.image.Handle());
			querySucceeded = imageInfo.has_value();
			if (!querySucceeded) {
				const auto result = a_client.LastResult();
				if (!cached.loggedFailure || *cached.loggedFailure != result) {
					L->warn(
						"Debug image query failed for {}: {}",
						a_feature.GetName(),
						DMUI_ResultToString(result));
					cached.loggedFailure = result;
				}
			} else {
				cached.loggedFailure.reset();
			}
		}
		if (host::ShouldImportImage(
				static_cast<bool>(cached.image.Handle()),
				cached.importFailure,
				sourceChanged,
				querySucceeded,
				imageInfo && imageInfo->status == DMUI_IMAGE_STATUS_READY,
				_hostFrameSerial >= cached.retryAfterFrame)) {
			const auto previousFailure =
				sourceChanged ? std::optional<DMUI_Result>{} : cached.loggedFailure;
			cached = {};
			cached.loggedFailure = previousFailure;
			auto imported = a_client.ImportD3D11Image(
				texture.texture, texture.width, texture.height);
			if (!imported) {
				cached.source = texture.texture;
				cached.width = texture.width;
				cached.height = texture.height;
				cached.generation = _debugImageGeneration;
				const auto result = a_client.LastResult();
				cached.importResult = result;
				cached.importFailure =
					result == DMUI_RESULT_UNSUPPORTED_RESOURCE ||
						result == DMUI_RESULT_INVALID_ARGUMENT ?
					host::ImageImportFailure::kPermanent :
					host::ImageImportFailure::kTransient;
				cached.retryAfterFrame = _hostFrameSerial + kImageRetryFrames;
				if (!cached.loggedFailure || *cached.loggedFailure != result) {
					D3D11_SHADER_RESOURCE_VIEW_DESC descriptor{};
					texture.texture->GetDesc(&descriptor);
					L->warn(
						"Debug image import failed for {} view {}: {} (result={}, format={}, dimension={})",
						a_feature.GetName(),
						selectedId,
						DMUI_ResultToString(result),
						result,
						static_cast<unsigned>(descriptor.Format),
						static_cast<unsigned>(descriptor.ViewDimension));
					cached.loggedFailure = result;
				}
				ImGui::TextDisabled(
					"Image import failed: %s (code %u). See the cs.menu log.",
					DMUI_ResultToString(cached.importResult),
					cached.importResult);
				return;
			}
			cached.source = texture.texture;
			cached.width = texture.width;
			cached.height = texture.height;
			cached.generation = _debugImageGeneration;
			cached.image = std::move(*imported);
		}
		if (cached.importFailure != host::ImageImportFailure::kNone) {
			ImGui::TextDisabled(
				"Image import failed: %s (code %u). See the cs.menu log.",
				DMUI_ResultToString(cached.importResult),
				cached.importResult);
			return;
		}

		const DMUI_ImageDrawOptions options{
			DMUI_IMAGE_DRAW_OPTIONS_0_1_SIZE,
			{ (std::max)(1.0f, ImGui::GetContentRegionAvail().x), 0.0f },
			{ 0.0f, 0.0f },
			{ 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			1u,
			0u
		};
		if (!a_client.DrawImage(cached.image.Handle(), options)) {
			L->warn(
				"Debug image draw failed for {}: {}",
				a_feature.GetName(),
				DMUI_ResultToString(a_client.LastResult()));
		}
	}

	void Menu::DrawHome(dmui::Client& a_client)
	{
		{
			dmui::FontGuard title{ a_client, DMUI_FONT_ROLE_TITLE };
			ImGui::TextUnformatted("Fallout 4 Community Shaders");
		}
		ImGui::TextWrapped(
			"Modern rendering features for Fallout 4. Features ship disabled; "
			"choose which features to load in Advanced, then restart. "
			"Use Enabled on each loaded feature's page to toggle its effect live.");

		std::size_t installed{};
		std::size_t active{};
		for (const auto* feature : FeatureManager::Get().GetRegisteredFeatures()) {
			if (!feature)
				continue;
			installed += feature->IsInstalled() ? 1u : 0u;
			active += feature->IsActive() ? 1u : 0u;
		}
		ImGui::Text(
			"Features installed: %zu of %zu (%zu loaded)",
			installed,
			FeatureManager::Get().GetRegisteredFeatures().size(),
			active);

		DrawFeatureOverview(a_client);

		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Links"),
				"draw links section"))
			return;
		const std::array links{
			dmui::Link{
				"GitHub",
				"https://github.com/northaxosky/fallout4-community-shaders",
				"Copies the project URL.",
				0,
				true },
			dmui::Link{
				"Nexus Mods",
				nullptr,
				"Not available yet.",
				0,
				false }
		};
		if (!CheckHostResult(
				a_client,
				a_client.DrawLinkRow("community-shaders-links", links),
				"draw project links"))
			return;

		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("FAQ"),
				"draw FAQ section"))
			return;
		const std::array faq{
			dmui::FaqEntry{
				"Why is nothing enabled?",
				"Every feature ships disabled. Check it under Advanced > Load on startup, then restart." },
			dmui::FaqEntry{
				"Where are settings stored?",
				"Defaults remain in FO4CommunityShaders.toml. Changes are written to FO4CommunityShaders.User.toml." },
			dmui::FaqEntry{
				"Why is there no standalone menu?",
				"Community Shaders is forwarding-only. Without a compatible DearModdingUI host, shader features continue headless." }
		};
		(void)CheckHostResult(
			a_client,
			a_client.DrawFaq("community-shaders-faq", faq),
			"draw FAQ");
	}

	void Menu::DrawFeatureOverview(dmui::Client& a_client)
	{
		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Feature status"),
				"draw feature status section"))
			return;
		ImGui::TextWrapped(
			"Loaded means the feature was loaded at startup. "
			"Its Enabled setting controls whether the effect is currently applied.");
		ui::SettingsTableScope table{ a_client, "home-feature-status" };
		if (!table.Valid() || !table.Visible())
			return;
		for (const auto* feature : FeatureManager::Get().GetRegisteredFeatures()) {
			if (!feature || !feature->IsInMenu())
				continue;
			const auto& state = feature->GetState();
			const char* status = "Not loaded";
			DMUI_StatusSeverity severity = DMUI_STATUS_SEVERITY_INFO;
			switch (state.runtimeState) {
			case FeatureRuntimeState::kActive:
				status = "Loaded";
				severity = DMUI_STATUS_SEVERITY_SUCCESS;
				break;
			case FeatureRuntimeState::kDegraded:
				status = "Unavailable";
				severity = DMUI_STATUS_SEVERITY_WARNING;
				break;
			case FeatureRuntimeState::kFailed:
				status = "Failed";
				severity = DMUI_STATUS_SEVERITY_ERROR;
				break;
			default:
				break;
			}
			const auto id = std::format("feature-status-{}", feature->GetName());
			const auto label = std::string(feature->GetDisplayName());
			ui::SettingsRowScope row{
				a_client,
				id.c_str(),
				label.c_str(),
				"Startup loading result; live effect controls are on the feature page." };
			if (!row.Valid())
				return;
			if (row.Visible()) {
				ImGui::TextColored(ThemeColor(a_client, severity), "%s", status);
				if (!state.detail.empty())
					ImGui::TextWrapped("%s", state.detail.c_str());
			}
		}
	}

	void Menu::DrawShaderSettings(dmui::Client& a_client)
	{
		const auto ownership =
			feature_config::ParseShaderOwnership(feature_config::GetMergedRoot());
		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Shader Ownership"),
				"draw shader ownership section"))
			return;
		{
			ui::SettingsTableScope table{ a_client, "advanced-shader-ownership" };
			if (!table.Valid())
				return;
			if (table.Visible()) {
				{
					ui::SettingsRowScope status{
						a_client,
						"shader-ownership-status",
						"Status",
						"Applied at boot only when the stock shader hash matches." };
					if (!status.Valid())
						return;
					if (status.Visible()) {
						const auto severity = ownership.config.enabled ?
							DMUI_STATUS_SEVERITY_SUCCESS :
							DMUI_STATUS_SEVERITY_INFO;
						ImGui::TextColored(
							ThemeColor(a_client, severity),
							"%s",
							ownership.config.enabled ? "Enabled" : "Disabled");
					}
				}

				struct Target
				{
					const char* id;
					const char* label;
					bool enabled;
				};
				const std::array targets{
					Target{ "deferred-prepass", "Deferred prepass", ownership.config.targets.deferredPrepass },
					Target{ "bssky", "BSSky", ownership.config.targets.bsSky },
					Target{ "bswater", "BSWater", ownership.config.targets.bsWater },
					Target{ "bslighting", "BSLighting", ownership.config.targets.bsLighting },
					Target{ "bsdf-light", "BSDF light", ownership.config.targets.bsdfLight },
					Target{ "bsdf-composite", "BSDF composite", ownership.config.targets.bsdfComposite },
					Target{ "df-tiled-lighting", "DFTiledLighting", ownership.config.targets.dfTiledLighting }
				};
				for (const auto& target : targets) {
					ui::SettingsRowScope row{
						a_client,
						target.id,
						target.label,
						"Read-only boot configuration from the unified TOML." };
					if (!row.Valid())
						return;
					if (row.Visible()) {
						auto enabled = target.enabled;
						ImGui::BeginDisabled();
						(void)ImGui::Checkbox("##enabled", &enabled);
						ImGui::EndDisabled();
					}
				}
				if (!ownership.valid) {
					ui::SettingsRowScope error{
						a_client,
						"shader-ownership-error",
						"Configuration error",
						"",
						dmui::RowPresentation::Layout::kFullSpan };
					if (!error.Valid())
						return;
					if (error.Visible()) {
						ImGui::TextColored(
							ThemeColor(a_client, DMUI_STATUS_SEVERITY_ERROR),
							"%s",
							ownership.error.c_str());
					}
				}
			}
		}

		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Shader Cache"),
				"draw shader cache section"))
			return;
		{
			ui::SettingsTableScope table{ a_client, "advanced-shader-cache" };
			if (!table.Valid())
				return;
			if (table.Visible()) {
				const auto cacheRoot = shader_cache::DefaultCacheRoot();
				{
					ui::SettingsRowScope location{
						a_client,
						"shader-cache-location",
						"Cache directory",
						"Compiled shader records are stored here." };
					if (!location.Valid())
						return;
					if (location.Visible())
						ImGui::TextWrapped("%s", cacheRoot.string().c_str());
				}

				ui::SettingsRowScope open{
					a_client,
					"open-shader-cache",
					"Open cache folder",
					"Open the physical cache location. Under MO2 this may be in Overwrite." };
				if (!open.Valid())
					return;
				if (open.Visible() && ImGui::Button("Open")) {
					const auto identity = cacheRoot / shader_cache::kIdentityFileName;
					std::error_code error;
					if (!std::filesystem::exists(identity, error) && !error) {
						ShowToast("The shader cache is not initialized. Restart to initialize it.", 4.0);
					} else {
						OpenFileLocation(identity);
					}
				}
			}
		}
	}

	void Menu::DrawAdvanced(dmui::Client& a_client)
	{
		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Configuration"),
				"draw configuration section"))
			return;
		{
			ui::SettingsTableScope table{ a_client, "advanced-configuration" };
			if (!table.Valid())
				return;
			if (table.Visible()) {
				ui::SettingsRowScope folder{
					a_client,
					"open-configuration-folder",
					"Configuration file location",
					"Open the physical location of your User TOML, or the Default TOML if no User file exists. MO2 can store them in different folders." };
				if (!folder.Valid())
					return;
				if (folder.Visible() && ImGui::Button("Open")) {
					std::error_code error;
					const bool userExists = std::filesystem::exists(
						feature_config::kUserConfigPath, error);
					if (error) {
						L->warn("Cannot inspect the User TOML location: {}", error.message());
						ShowToast("Could not locate the User TOML; see log.", 4.0, DMUI_STATUS_SEVERITY_ERROR);
					} else {
						OpenFileLocation(userExists ?
							feature_config::kUserConfigPath :
							feature_config::kDefaultConfigPath);
					}
				}
			}
		}

		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Load on startup"),
				"draw boot settings section"))
			return;
		{
			ImGui::TextWrapped(
				"Checked features load when the game starts. Changes require a restart. "
				"Use Enabled on a feature's page to switch its effect on or off now.");
			ui::SettingsTableScope table{ a_client, "advanced-startup-loading" };
			if (!table.Valid())
				return;
			if (table.Visible()) {
				auto features = FeatureManager::Get().GetRegisteredFeatures();
				std::ranges::sort(
					features,
					{},
					[](const Feature* a_feature) {
						return a_feature->GetDisplayName();
					});
				for (auto* feature : features) {
					if (!feature || !feature->IsInMenu())
						continue;
					const auto featureConfig =
						feature_config::GetFeature(feature->GetConfigKey());
					bool loadAtBoot = featureConfig &&
						featureConfig->get("load") &&
						featureConfig->get("load")->value_or(false);
					const auto id =
						std::format("load-on-startup-{}", feature->GetName());
					const auto label = std::string(feature->GetDisplayName());
					ui::SettingsRowScope row{
						a_client,
						id.c_str(),
						label.c_str(),
						"Checked: load this feature on the next launch. Requires restart." };
					if (!row.Valid())
						return;
					if (row.Visible() &&
						ImGui::Checkbox("##load", &loadAtBoot)) {
						const auto result = feature_config::UpdateFeatureLoad(
							feature->GetConfigKey(), loadAtBoot);
						if (!result) {
							L->warn(
								"Failed to save boot state for {}: {}",
								feature->GetName(),
								result.error);
						} else {
							(void)feature_config::Reload();
						}
					}
					if (row.Visible() &&
						_startupLoads.RequiresRestart(feature->GetConfigKey(), loadAtBoot)) {
						ImGui::SameLine();
						ImGui::TextColored(
							ThemeColor(a_client, DMUI_STATUS_SEVERITY_WARNING),
							"Restart required");
					}
				}
			}
		}

		DrawShaderSettings(a_client);

		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Logging"),
				"draw logging section"))
			return;
		{
			static constexpr std::array<const char*, 7> levelNames{
				"Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off"
			};
			static constexpr std::array levels{
				spdlog::level::trace,
				spdlog::level::debug,
				spdlog::level::info,
				spdlog::level::warn,
				spdlog::level::err,
				spdlog::level::critical,
				spdlog::level::off
			};
			const auto levelIndex = [&](spdlog::level::level_enum a_level) {
				const auto found = std::ranges::find(levels, a_level);
				return found == levels.end() ?
					2 :
					static_cast<int>(std::distance(levels.begin(), found));
			};

			ui::SettingsTableScope table{ a_client, "advanced-logging" };
			if (!table.Valid())
				return;
			if (table.Visible()) {
				ui::SettingsRowScope global{
					a_client,
					"global-log-level",
					"Global level",
					"Default severity threshold for Community Shaders loggers." };
				if (!global.Valid())
					return;
				if (global.Visible()) {
					auto selected = levelIndex(log::GlobalLevel());
					if (DrawChoice("##level", selected, levelNames)) {
						log::SetGlobalLevel(levels[static_cast<std::size_t>(selected)]);
						(void)log::SaveConfigToToml();
					}
				}
			}
		}

		static std::string loggerSearch;
		if (!a_client.DrawSearchInput(
				"advanced-logger-search",
				"Search logging channels...",
				loggerSearch)) {
			CheckHostResult(a_client, false, "draw logger search");
			return;
		}
		{
			ui::SettingsTableScope table{ a_client, "advanced-log-channels" };
			if (!table.Valid())
				return;
			if (table.Visible()) {
				static constexpr std::array<const char*, 7> levelNames{
					"Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off"
				};
				static constexpr std::array levels{
					spdlog::level::trace,
					spdlog::level::debug,
					spdlog::level::info,
					spdlog::level::warn,
					spdlog::level::err,
					spdlog::level::critical,
					spdlog::level::off
				};
				for (const auto& name : log::ListLoggers()) {
					if (!dmui::ContainsFolded(name, loggerSearch))
						continue;
					auto* logger = log::Get(name.c_str());
					const auto found = std::ranges::find(
						levels,
						logger ? logger->level() : log::GlobalLevel());
					auto selected = found == levels.end() ?
						2 :
						static_cast<int>(std::distance(levels.begin(), found));
					const auto id = "log-channel-" + name;
					ui::SettingsRowScope row{
						a_client,
						id.c_str(),
						name.c_str(),
						"Overrides the global logging level for this channel." };
					if (!row.Valid())
						return;
					if (row.Visible() &&
						DrawChoice("##level", selected, levelNames)) {
						log::SetLevel(
							name.c_str(),
							levels[static_cast<std::size_t>(selected)]);
						(void)log::SaveConfigToToml();
					}
				}
			}
		}

		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Telemetry"),
				"draw telemetry section"))
			return;
		{
			ui::SettingsTableScope table{ a_client, "advanced-telemetry" };
			if (!table.Valid())
				return;
			if (table.Visible()) {
				{
					ui::SettingsRowScope enabledRow{
						a_client,
						"telemetry-enabled",
						"Emit telemetry",
						"Collect cached feature and frame diagnostics for log dumps." };
					if (!enabledRow.Valid())
						return;
					if (enabledRow.Visible()) {
						bool enabled = telemetry::pump::Enabled();
						if (ImGui::Checkbox("##enabled", &enabled)) {
							telemetry::pump::SetEnabled(enabled);
							(void)log::SaveConfigToToml();
						}
					}
				}
				ui::SettingsRowScope dump{
					a_client,
					"telemetry-dump",
					"Dump now",
					"Write the current diagnostic snapshot to the log." };
				if (!dump.Valid())
					return;
				if (dump.Visible() && ImGui::Button("Dump"))
					telemetry::pump::RequestDump();
			}
		}

		if (!CheckHostResult(
				a_client,
				a_client.DrawSectionHeader("Diagnostics"),
				"draw diagnostics section"))
			return;
		{
			ui::SettingsTableScope table{ a_client, "advanced-diagnostics" };
			if (!table.Valid())
				return;
			if (table.Visible()) {
				const std::array values{
					std::pair{ "Plugin version", Plugin::VERSION.string(".") },
					std::pair{ "Build", std::string(CS_BUILD_DESCRIBE) },
					std::pair{ "Commit", std::string(CS_BUILD_GIT_SHA) },
					std::pair{ "GPU adapter", AdapterDescription() }
				};
				for (std::size_t index = 0; index < values.size(); ++index) {
					const auto id = std::format("diagnostic-{}", index);
					ui::SettingsRowScope row{
						a_client,
						id.c_str(),
						values[index].first,
						"" };
					if (!row.Valid())
						return;
					if (row.Visible())
						ImGui::TextWrapped("%s", values[index].second.c_str());
				}
			}
		}
	}

	void Menu::DrawPresets(dmui::Client& a_client)
	{
		auto& presets = PresetManager::Get();
		const auto& list = presets.List();
		if (presets.pendingComboIdentity.empty() && !list.empty())
			presets.pendingComboIdentity = list.front().identity;

		const auto* pending = presets.FindByIdentity(presets.pendingComboIdentity);
		const auto* active = presets.FindByIdentity(presets.activeIdentity);
		ui::SettingsTableScope table{ a_client, "presets" };
		if (!table.Valid() || !table.Visible())
			return;
		{
			ui::SettingsRowScope row{
				a_client,
				"active-preset",
				"Active preset",
				"The last preset applied to live feature settings." };
			if (!row.Valid())
				return;
			if (row.Visible()) {
				ImGui::TextUnformatted(
					presets.activeName.empty() ?
						"(none)" :
						presets.activeName.c_str());
			}
		}
		if (!presets.lastError.empty()) {
			ui::SettingsRowScope row{
				a_client,
				"preset-error",
				"Preset error",
				"",
				dmui::RowPresentation::Layout::kFullSpan };
			if (!row.Valid())
				return;
			if (row.Visible()) {
				ImGui::TextColored(
					ThemeColor(a_client, DMUI_STATUS_SEVERITY_ERROR),
					"%s",
					presets.lastError.c_str());
			}
		}

		const std::string preview = pending ?
			std::format("{}: {}", pending->builtin ? "B" : "U", pending->name) :
			"(no presets found)";
		{
			ui::SettingsRowScope row{
				a_client,
				"preset-selection",
				"Preset",
				"Choose a built-in or user preset." };
			if (!row.Valid())
				return;
			if (row.Visible()) {
				ImGui::BeginDisabled(list.empty());
				if (ImGui::BeginCombo("##preset", preview.c_str())) {
					for (const auto& preset : list) {
						const auto label =
							std::format("{}: {}", preset.builtin ? "B" : "U", preset.name);
						if (ImGui::Selectable(
								label.c_str(),
								preset.identity == presets.pendingComboIdentity))
							presets.pendingComboIdentity = preset.identity;
					}
					ImGui::EndCombo();
				}
				ImGui::EndDisabled();
			}
		}

		pending = presets.FindByIdentity(presets.pendingComboIdentity);
		active = presets.FindByIdentity(presets.activeIdentity);
		ui::SettingsRowScope actions{
			a_client,
			"preset-actions",
			"Actions",
			"Load, save, copy, delete, or rescan presets.",
			dmui::RowPresentation::Layout::kFullSpan };
		if (!actions.Valid())
			return;
		if (!actions.Visible())
			return;
		ImGui::BeginDisabled(!pending);
		if (ImGui::Button("Load") && pending) {
			std::string error;
			if (!presets.Apply(*pending, error))
				presets.lastError = "Load failed: " + error;
			else
				presets.lastError.clear();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!active || active->builtin);
		if (ImGui::Button("Save") && active && !active->builtin) {
			const auto identity = active->identity;
			const auto name = active->name;
			const auto path = active->path;
			std::string error;
			if (!presets.Save(path, name, error, true)) {
				presets.lastError = "Save failed: " + error;
			} else {
				presets.Refresh();
				presets.activeIdentity = identity;
				presets.activeName = name;
				presets.pendingComboIdentity = identity;
				presets.lastError.clear();
			}
			active = presets.FindByIdentity(presets.activeIdentity);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Save As...") &&
			_dialog.operation == DialogOperation::kNone) {
			const DMUI_DialogDescriptor descriptor{
				DMUI_DIALOG_DESCRIPTOR_0_1_SIZE,
				DMUI_DIALOG_KIND_TEXT_ENTRY,
				"Save Preset As",
				"Enter a unique preset name using ASCII letters, digits, underscore, or hyphen.",
				"Save",
				"Cancel",
				"Preset name (1-64 characters)",
				"",
				65u
			};
			StartDialog(a_client, DialogOperation::kSavePresetAs, descriptor);
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(!active || active->builtin);
		if (ImGui::Button("Delete") && active && !active->builtin &&
			_dialog.operation == DialogOperation::kNone) {
			_dialog.presetIdentity = active->identity;
			_dialog.presetName = active->name;
			_dialog.presetPath = active->path;
			const auto body = std::format(
				"Delete preset '{}'? File is removed from disk. This cannot be undone.",
				active->name);
			const DMUI_DialogDescriptor descriptor{
				DMUI_DIALOG_DESCRIPTOR_0_1_SIZE,
				DMUI_DIALOG_KIND_CONFIRM,
				"Delete Preset",
				body.c_str(),
				"Delete",
				"Cancel",
				nullptr,
				nullptr,
				1u
			};
			StartDialog(a_client, DialogOperation::kDeletePreset, descriptor);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			presets.Refresh();
			if (!presets.FindByIdentity(presets.pendingComboIdentity))
				presets.pendingComboIdentity.clear();
			presets.lastError.clear();
		}
		(void)actions.End();

		{
			ui::SettingsRowScope row{
				a_client,
				"preset-auto-load",
				"Auto-load on boot",
				"Apply the active preset during startup." };
			if (!row.Valid())
				return;
			if (row.Visible() &&
				ImGui::Checkbox("##auto-load", &presets.autoLoadOnBoot))
				(void)presets.SaveCoreConfig();
		}
	}

	void Menu::ObserveHostFrame(dmui::Client& a_client)
	{
		++_hostFrameSerial;
		ProcessDialog(a_client);
		if (!_clearCacheRequested.exchange(false, std::memory_order_acq_rel))
			return;
		if (_dialog.operation != DialogOperation::kNone) {
			ShowToast("Finish the current dialog before clearing the shader cache.", 4.0);
			return;
		}
		const DMUI_DialogDescriptor descriptor{
			DMUI_DIALOG_DESCRIPTOR_0_1_SIZE,
			DMUI_DIALOG_KIND_CONFIRM,
			"Clear Shader Cache",
			"This deletes every compiled shader record on disk. Shaders are recompiled the next time they are needed.",
			"Clear",
			"Cancel",
			nullptr,
			nullptr,
			1u
		};
		StartDialog(a_client, DialogOperation::kClearCache, descriptor);
	}

	void Menu::OnHostDeviceReady() noexcept
	{
		++_debugImageGeneration;
		ReleaseDebugImages();
	}

	void Menu::StartDialog(
		dmui::Client& a_client,
		DialogOperation a_operation,
		const DMUI_DialogDescriptor& a_descriptor)
	{
		const auto dialog = a_client.RequestDialog(a_descriptor);
		if (!dialog) {
			L->warn(
				"Dialog request failed: {}",
				DMUI_ResultToString(a_client.LastResult()));
			return;
		}
		_dialog.operation = a_operation;
		_dialog.handle = *dialog;
		_dialog.submissions.Reset();
		_dialog.polling.Succeeded();
		_dialog.resolution.Succeeded();
		_dialog.text.clear();
	}

	void Menu::ProcessDialog(dmui::Client& a_client)
	{
		if (_dialog.handle == DMUI_INVALID_DIALOG_HANDLE ||
			!_dialog.polling.Ready(_hostFrameSerial))
			return;
		const auto event = a_client.PollDialogEvent(_dialog.handle, _dialog.text);
		if (!event) {
			HandleDialogFailure(
				"polling", a_client.LastResult(), _dialog.polling);
			return;
		}
		_dialog.polling.Succeeded();
		if (event->kind == DMUI_DIALOG_EVENT_SUBMITTED &&
			_dialog.submissions.ShouldExecute(event->submissionId)) {
			try {
				auto outcome = ExecuteDialogSubmission(*event, _dialog.text);
				_dialog.submissions.RecordOutcome(
					outcome.submission,
					outcome.accepted,
					std::move(outcome.error));
			} catch (const std::exception& error) {
				_dialog.submissions.RecordOutcome(
					event->submissionId,
					false,
					std::format("Operation failed: {}", error.what()));
			} catch (...) {
				_dialog.submissions.RecordOutcome(
					event->submissionId,
					false,
					"Operation failed with a non-standard exception.");
			}
			RetryDialogResolution(a_client, *event);
		} else if (event->kind == DMUI_DIALOG_EVENT_SUBMITTED &&
			_dialog.submissions.Pending(event->submissionId)) {
			RetryDialogResolution(a_client, *event);
		} else if (event->kind == DMUI_DIALOG_EVENT_CANCELLED ||
			event->kind == DMUI_DIALOG_EVENT_COMPLETED) {
			_dialog = {};
		}
	}

	host::DialogSubmissionTracker::Outcome Menu::ExecuteDialogSubmission(
		const DMUI_DialogEvent& a_event,
		std::string_view a_text)
	{
		auto& presets = PresetManager::Get();
		std::string error;
		bool accepted{};
		switch (_dialog.operation) {
		case DialogOperation::kClearCache:
			accepted = ClearShaderCache(error);
			break;
		case DialogOperation::kSavePresetAs:
			if (!ValidatePresetName(a_text, presets.List(), error)) {
				error = "Invalid name: " + error;
				break;
			}
			{
				const std::string name(a_text);
				const auto path =
					std::filesystem::path(kPresetRoot) / (name + ".toml");
				if (!presets.Save(path, name, error))
					break;
				presets.Refresh();
				if (const auto* saved = presets.FindByName(name, true)) {
					presets.activeIdentity = saved->identity;
					presets.activeName = saved->name;
					presets.pendingComboIdentity = saved->identity;
				}
				if (!presets.SaveCoreConfig()) {
					error =
						"Preset was saved, but active preset configuration could not be persisted.";
					break;
				}
				presets.lastError.clear();
				accepted = true;
			}
			break;
		case DialogOperation::kDeletePreset:
			{
				PresetMeta target{
					_dialog.presetName,
					_dialog.presetIdentity,
					_dialog.presetPath,
					false };
				if (!presets.Delete(target, error))
					break;
				presets.Refresh();
				presets.activeIdentity.clear();
				presets.activeName.clear();
				presets.pendingComboIdentity.clear();
				presets.autoLoadOnBoot = false;
				if (!presets.SaveCoreConfig()) {
					error =
						"Preset was deleted, but preset configuration could not be persisted.";
					break;
				}
				presets.lastError.clear();
				accepted = true;
			}
			break;
		default:
			error = "No dialog operation is active.";
			break;
		}

		return {
			a_event.submissionId,
			accepted,
			std::move(error)
		};
	}

	void Menu::RetryDialogResolution(
		dmui::Client& a_client,
		const DMUI_DialogEvent& a_event)
	{
		const auto* outcome =
			_dialog.submissions.Pending(a_event.submissionId);
		if (!outcome || !_dialog.resolution.Ready(_hostFrameSerial))
			return;
		if (!a_client.ResolveDialogSubmission(
				_dialog.handle,
				outcome->submission,
				outcome->accepted,
				outcome->accepted ? nullptr : outcome->error.c_str())) {
			HandleDialogFailure(
				"resolution", a_client.LastResult(), _dialog.resolution);
			return;
		}
		_dialog.resolution.Succeeded();
		if (!outcome->accepted &&
			(_dialog.operation == DialogOperation::kSavePresetAs ||
				_dialog.operation == DialogOperation::kDeletePreset))
			PresetManager::Get().lastError = outcome->error;
		_dialog.submissions.ResolutionSucceeded(outcome->submission);
	}

	void Menu::HandleDialogFailure(
		std::string_view a_operation,
		DMUI_Result a_result,
		host::DialogCallRetry& a_retry)
	{
		if (a_retry.Failed(a_result, _hostFrameSerial)) {
			L->warn(
				"Dialog {} failed: {}",
				a_operation,
				DMUI_ResultToString(a_result));
		}
		if (host::DialogCallRetry::HandleLost(a_result)) {
			L->warn("The host no longer owns this dialog; clearing its local request state.");
			_dialog = {};
		}
		// Transient failures retain submitted outcomes so retries never repeat disk operations.
	}

	bool Menu::ClearShaderCache(std::string& a_error)
	{
		const auto root = shader_cache::DefaultCacheRoot();
		std::error_code error;
		const auto removed = shader_cache::ClearCacheRecords(root, error);
		if (error) {
			L->warn("Failed to clear shader cache at '{}': {}", root.string(), error.message());
			ShowToast(
				"Failed to clear the shader cache (see log)",
				4.0,
				DMUI_STATUS_SEVERITY_ERROR);
			a_error = "Failed to clear the shader cache; see the log.";
			return false;
		}
		L->info("Cleared {} shader cache entries from '{}'", removed, root.string());
		ShowToast("Shader cache cleared", 2.5);
		return true;
	}

	void Menu::RequestClearShaderCache() noexcept
	{
		_clearCacheRequested.store(true, std::memory_order_release);
	}

	bool Menu::CheckHostResult(
		dmui::Client& a_client,
		bool a_succeeded,
		std::string_view a_operation)
	{
		const std::string operation(a_operation);
		if (a_succeeded) {
			_hostCallFailures.erase(operation);
			return true;
		}

		const auto result = a_client.LastResult();
		const auto previous = _hostCallFailures.find(operation);
		if (previous == _hostCallFailures.end() || previous->second != result) {
			L->warn(
				"DearModdingUI {} failed: {}",
				a_operation,
				DMUI_ResultToString(result));
			_hostCallFailures.insert_or_assign(operation, result);
		}
		return false;
	}

	void Menu::ReleaseDebugImages() noexcept
	{
		_debugImages.clear();
	}

	void Menu::ShowToast(
		std::string a_text,
		double a_durationSec,
		DMUI_StatusSeverity a_severity)
	{
		host::HostClient::Get().PostNotification(
			a_severity,
			std::move(a_text),
			static_cast<std::uint32_t>(
				std::clamp(a_durationSec * 1000.0, 250.0, 30000.0)));
	}

}
