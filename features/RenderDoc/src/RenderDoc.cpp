#include "RenderDoc.h"

#include <renderdoc_app.h>

#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "FrameGeneration.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Settings/FeatureConfig.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.renderdoc"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\RenderDoc.toml";
	constexpr double      kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;
	constexpr int         kMinMultiFrameCount = 2;
	constexpr int         kMaxMultiFrameCount = 60;

	RenderDoc* RenderDoc::GetSingleton()
	{
		static RenderDoc singleton;
		return &singleton;
	}

	namespace
	{
		// RenderDoc's early D3D/DXGI detours break DLSS-G fallback; refuse to load when DLSS-G is on.
		bool DLSSGRequested()
		{
			const auto* frameGeneration = FrameGeneration::GetSingleton();
			return frameGeneration->IsActive()
				&& frameGeneration->settings.frameGenerationMode
				&& frameGeneration->settings.frameGenType == static_cast<int>(FrameGeneration::FrameGenType::kDLSSG);
		}

		int ClampMultiFrameCount(int64_t a_value)
		{
			return static_cast<int>(std::clamp(a_value,
				static_cast<int64_t>(kMinMultiFrameCount),
				static_cast<int64_t>(kMaxMultiFrameCount)));
		}

		double ClampMinFreeDiskGiB(double a_value)
		{
			return a_value >= 0.0 ? a_value : RenderDoc::Settings{}.minFreeDiskGiB;
		}

		std::string SettingError(std::string_view a_key, std::string_view a_reason)
		{
			return "settings." + std::string(a_key) + ": " + std::string(a_reason);
		}

		bool AcceptSetting(
			feature_config::ScalarReadStatus a_status,
			std::string_view a_key,
			std::string_view a_expected,
			std::string_view a_range,
			std::string& a_error)
		{
			switch (a_status) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = SettingError(a_key, "expected " + std::string(a_expected));
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = SettingError(a_key, "value must be finite");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = SettingError(a_key, a_range);
				break;
			}
			return false;
		}

		bool ReadIntegerSetting(
			const toml::table& a_table,
			std::string_view a_key,
			std::int64_t a_min,
			std::int64_t a_max,
			int& a_value,
			std::string& a_error)
		{
			auto value = static_cast<std::int64_t>(a_value);
			const auto status = feature_config::ReadSignedInteger(a_table, a_key, value, a_min, a_max);
			if (!AcceptSetting(status, a_key, "integer", "value must be in range 2..60", a_error)) {
				return false;
			}
			if (status == feature_config::ScalarReadStatus::kValid) {
				a_value = static_cast<int>(value);
			}
			return true;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			RenderDoc::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode) {
				return true;
			}

			const auto* settingsTable = settingsNode->as_table();
			if (!settingsTable) {
				a_error = "settings: expected table";
				return false;
			}

			return AcceptSetting(
					feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled),
					"enabled", "boolean", "boolean value is out of range", a_error)
				&& AcceptSetting(
					feature_config::ReadString(*settingsTable, "dll_path", a_candidate.dllPath),
					"dll_path", "string", "string value is out of range", a_error)
				&& AcceptSetting(
					feature_config::ReadString(*settingsTable, "capture_folder", a_candidate.captureFolder),
					"capture_folder", "string", "string value is out of range", a_error)
				&& AcceptSetting(
					feature_config::ReadDouble(
						*settingsTable,
						"min_free_disk_gib",
						a_candidate.minFreeDiskGiB,
						0.0,
						std::numeric_limits<double>::max()),
					"min_free_disk_gib", "number", "value must be greater than or equal to 0", a_error)
				&& ReadIntegerSetting(
					*settingsTable,
					"multi_frame_count",
					kMinMultiFrameCount,
					kMaxMultiFrameCount,
					a_candidate.multiFrameCount,
					a_error);
		}
	}

	bool RenderDoc::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		return true;
	}

	std::optional<bool> RenderDoc::GetLegacyActivationIntent(const toml::table& a_config) const
	{
		const auto* settingsNode = a_config.get("settings");
		const auto* settingsTable = settingsNode ? settingsNode->as_table() : nullptr;
		const auto* enabledNode = settingsTable ? settingsTable->get("enabled") : nullptr;
		if (!enabledNode || !enabledNode->is_boolean()) {
			return std::nullopt;
		}
		return enabledNode->as_boolean()->get();
	}

	void RenderDoc::Load()
	{
		L->info("Settings: enabled={} dll={} folder={} min_free_disk_gib={:.2f} multi_frame_count={}",
			_settings.enabled, _settings.dllPath, _settings.captureFolder,
			_settings.minFreeDiskGiB, _settings.multiFrameCount);

		if (!_settings.enabled)
			return;
		if (DLSSGRequested()) {
			constexpr std::string_view reason =
				"RenderDoc cannot start while active FrameGeneration uses DLSS-G; "
				"disable DLSS-G or RenderDoc and restart";
			L->warn("{}", reason);
			FailLoad(std::string(reason));
			return;
		}
		// Load before any D3D device exists; loading post-D3D-init is unreliable.
		if (!TryLoadRuntime()) {
			FailLoad("RenderDoc runtime load failed for settings.dll_path '" + _settings.dllPath
				+ "'; verify the path and RenderDoc 1.7 API compatibility");
			return;
		}

		cs::Menu::Get().RegisterWndProcCallback(*this, &RenderDoc::HandleWndProc);
	}

	void RenderDoc::SaveSettings()
	{
		try {
			const std::filesystem::path configPath(kConfigPath);
			toml::table table;
			if (std::filesystem::exists(configPath)) {
				table = toml::parse_file(kConfigPath);
			}

			auto& settings = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
			settings.insert_or_assign("enabled", _settings.enabled);
			settings.insert_or_assign("dll_path", _settings.dllPath);
			settings.insert_or_assign("capture_folder", _settings.captureFolder);
			settings.insert_or_assign("min_free_disk_gib", _settings.minFreeDiskGiB);
			settings.insert_or_assign("multi_frame_count", static_cast<int64_t>(_settings.multiFrameCount));

			if (const auto parent = configPath.parent_path(); !parent.empty()) {
				std::filesystem::create_directories(parent);
			}

			std::ofstream out(configPath);
			if (!out) {
				L->error("Failed to open RenderDoc config for write: {}", kConfigPath);
				return;
			}
			out << table;
			out.flush();
			if (!out.good()) {
				L->error("Failed to write RenderDoc config: {}", kConfigPath);
			}
		} catch (const toml::parse_error& e) {
			L->error("Failed to parse RenderDoc config while saving {}: {}", kConfigPath, e.what());
		} catch (const std::filesystem::filesystem_error& e) {
			L->error("Failed to prepare RenderDoc config path {}: {}", kConfigPath, e.what());
		}
	}

	bool RenderDoc::TryLoadRuntime()
	{
		if (_api)
			return true;
		if (_attemptedLoad)
			return false;
		_attemptedLoad = true;

		// Loading renderdoc.dll after D3D devices exist can crash; call before D3D init only.
		_module = LoadLibraryA(_settings.dllPath.c_str());
		if (!_module) {
			L->warn("LoadLibrary({}) failed: {:#x}", _settings.dllPath, GetLastError());
			return false;
		}

		auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(_module, "RENDERDOC_GetAPI"));
		if (!getApi) {
			L->warn("RENDERDOC_GetAPI not found in {}", _settings.dllPath);
			FreeLibrary(_module);
			_module = nullptr;
			return false;
		}

		if (getApi(eRENDERDOC_API_Version_1_7_0, reinterpret_cast<void**>(&_api)) != 1 || !_api) {
			L->warn("RENDERDOC_GetAPI returned no API for version 1.7.0");
			_api = nullptr;
			FreeLibrary(_module);
			_module = nullptr;
			return false;
		}

		_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
		_api->SetCaptureKeys(nullptr, 0);

		ApplyCapturePath();
		L->info("RenderDoc runtime loaded");
		return true;
	}

	void RenderDoc::ApplyCapturePath()
	{
		if (!_api)
			return;
		std::error_code ec;
		std::filesystem::create_directories(_settings.captureFolder, ec);
		auto pathTemplate = (std::filesystem::path(_settings.captureFolder) / "FO4").string();
		_api->SetCaptureFilePathTemplate(pathTemplate.c_str());
	}

	bool RenderDoc::CheckCaptureDiskSpace() const
	{
		std::filesystem::path captureDir(_settings.captureFolder);
		if (captureDir.empty())
			captureDir = ".";

		std::error_code ec;
		std::filesystem::create_directories(captureDir, ec);
		if (ec) {
			L->warn("RenderDoc capture aborted: failed to prepare capture folder {}: {}",
				captureDir.string(), ec.message());
			cs::Menu::ShowToast("RenderDoc capture aborted: capture folder unavailable", 4.0);
			return false;
		}

		try {
			const auto availableBytes = std::filesystem::space(captureDir).available;
			const double availableGiB = static_cast<double>(availableBytes) / kBytesPerGiB;
			const double requiredGiB = ClampMinFreeDiskGiB(_settings.minFreeDiskGiB);
			if (availableGiB < requiredGiB) {
				L->warn("RenderDoc capture aborted: {:.2f} GiB free in {} below configured {:.2f} GiB",
					availableGiB, captureDir.string(), requiredGiB);
				cs::Menu::ShowToast("RenderDoc capture aborted: low disk space", 4.0);
				return false;
			}
		} catch (const std::filesystem::filesystem_error& e) {
			L->warn("RenderDoc capture aborted: failed to query free disk space for {}: {}",
				captureDir.string(), e.what());
			cs::Menu::ShowToast("RenderDoc capture aborted: disk check failed", 4.0);
			return false;
		}

		return true;
	}

	void RenderDoc::ApplyPendingComments()
	{
		if (!_api || !_commentsBuf[0])
			return;

		_api->SetCaptureFileComments("", _commentsBuf.data());
		_commentsBuf[0] = 0;
	}

	void RenderDoc::TriggerCapture()
	{
		if (!_settings.enabled) {
			L->warn("TriggerCapture called while feature disabled");
			return;
		}
		// Never LoadLibrary after D3D init; if startup load failed, a restart is required.
		if (!_api) {
			L->warn("RenderDoc runtime not loaded; restart the game with RenderDoc enabled to capture");
			return;
		}
		if (!CheckCaptureDiskSpace())
			return;

		_api->TriggerCapture();
		ApplyPendingComments();

		L->info("Single-frame capture triggered");
	}

	void RenderDoc::TriggerMultiFrameCapture()
	{
		if (!_settings.enabled) {
			L->warn("TriggerMultiFrameCapture called while feature disabled");
			return;
		}
		if (!_api) {
			L->warn("RenderDoc runtime not loaded; restart the game with RenderDoc enabled to capture");
			return;
		}
		if (!CheckCaptureDiskSpace())
			return;
		if (!_api->TriggerMultiFrameCapture) {
			L->warn("RenderDoc runtime does not expose TriggerMultiFrameCapture");
			return;
		}

		const auto frameCount = static_cast<uint32_t>(ClampMultiFrameCount(_settings.multiFrameCount));
		_api->TriggerMultiFrameCapture(frameCount);
		ApplyPendingComments();

		L->info("Multi-frame capture triggered: {} frames", frameCount);
	}

	bool RenderDoc::HandleWndProc(HWND, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
	{
		if (a_msg != WM_KEYDOWN || a_wparam != VK_F11 || (HIWORD(a_lparam) & KF_REPEAT) != 0)
			return false;

		auto* renderDoc = GetSingleton();
		if (!renderDoc->_settings.enabled)
			return false;

		if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
			renderDoc->TriggerMultiFrameCapture();
		else
			renderDoc->TriggerCapture();

		return true;
	}

	void RenderDoc::DrawSettings()
	{
		bool prevEnabled = _settings.enabled;
		if (ImGui::Checkbox("Enabled", &_settings.enabled)) {
			SaveSettings();
			if (_settings.enabled && !_api)
				L->warn("Enabled at runtime; restart the game to load renderdoc.dll safely");
			else if (!_settings.enabled && prevEnabled)
				L->info("Disabled; runtime stays loaded until process exit");
		}

		ImGui::TextDisabled("Restart after enabling. DLSS-G is blocked; disable all FrameGeneration for clean D3D11 captures.");
		ImGui::TextDisabled("F11 captures one frame. Shift+F11 captures the configured multi-frame count.");

		char dllPathBuf[260];
		strncpy_s(dllPathBuf, _settings.dllPath.c_str(), _TRUNCATE);
		if (ImGui::InputText("DLL path", dllPathBuf, sizeof(dllPathBuf)))
			_settings.dllPath = dllPathBuf;
		if (ImGui::IsItemDeactivatedAfterEdit())
			SaveSettings();

		char folderBuf[260];
		strncpy_s(folderBuf, _settings.captureFolder.c_str(), _TRUNCATE);
		if (ImGui::InputText("Capture folder", folderBuf, sizeof(folderBuf)))
			_settings.captureFolder = folderBuf;
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			SaveSettings();
			ApplyCapturePath();
		}

		if (ImGui::InputDouble("Minimum free disk (GiB)", &_settings.minFreeDiskGiB, 0.25, 1.0, "%.2f"))
			_settings.minFreeDiskGiB = ClampMinFreeDiskGiB(_settings.minFreeDiskGiB);
		if (ImGui::IsItemDeactivatedAfterEdit())
			SaveSettings();

		ImGui::SliderInt("Multi-frame count", &_settings.multiFrameCount, kMinMultiFrameCount, kMaxMultiFrameCount);
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			_settings.multiFrameCount = ClampMultiFrameCount(_settings.multiFrameCount);
			SaveSettings();
		}

		ImGui::InputTextMultiline("Comments (embedded in next .rdc)",
			_commentsBuf.data(), _commentsBuf.size(),
			ImVec2(0, ImGui::GetTextLineHeight() * 3));

		ImGui::BeginDisabled(!_api);
		if (ImGui::Button("Trigger Capture"))
			TriggerCapture();
		ImGui::SameLine();
		if (ImGui::Button("Trigger Multi-Frame"))
			TriggerMultiFrameCapture();
		ImGui::EndDisabled();

		if (!_api && _settings.enabled)
			ImGui::TextDisabled("Runtime load failed - fix the DLL path then restart the game.");
	}

	void RenderDoc::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
		ApplyCapturePath();
		L->info("Settings reset to defaults");
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(RenderDoc::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
