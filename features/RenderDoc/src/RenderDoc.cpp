#include "RenderDoc.h"

#include <renderdoc_app.h>

#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>

#include "F4SE/API.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "REX/CONVERT.h"
#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.renderdoc"); }

	constexpr double      kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;
	constexpr int         kMinMultiFrameCount = 2;
	constexpr int         kMaxMultiFrameCount = 60;
	constexpr std::string_view kLegacyCaptureFolder = "Data\\F4SE\\Plugins\\RenderDoc\\captures";

	RenderDoc* RenderDoc::GetSingleton()
	{
		static RenderDoc singleton;
		return &singleton;
	}

	namespace
	{
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

		std::string PathToUtf8(const std::filesystem::path& a_path)
		{
			std::string utf8;
			if (REX::UTF16_TO_UTF8(a_path.native(), utf8)) {
				return utf8;
			}
			return a_path.string();
		}

		std::filesystem::path ExpandCaptureFolderEnvironment(const std::string& a_configured)
		{
			std::wstring configured;
			if (!REX::UTF8_TO_UTF16(a_configured, configured)) {
				L->warn("Failed to decode RenderDoc capture folder as UTF-8; using it without environment expansion");
				return a_configured;
			}

			const auto requiredSize = REX::W32::ExpandEnvironmentStringsW(configured.c_str(), nullptr, 0);
			if (requiredSize == 0) {
				L->warn("Failed to expand environment variables in RenderDoc capture folder; using the configured path");
				return configured;
			}

			std::wstring expanded(requiredSize, L'\0');
			const auto expandedSize = REX::W32::ExpandEnvironmentStringsW(
				configured.c_str(), expanded.data(), requiredSize);
			if (expandedSize == 0 || expandedSize > requiredSize) {
				L->warn("Failed to expand environment variables in RenderDoc capture folder; using the configured path");
				return configured;
			}

			expanded.resize(expandedSize - 1);
			return expanded;
		}

		std::filesystem::path ResolveCaptureFolder(const std::string& a_configured)
		{
			if (!a_configured.empty()) {
				return ExpandCaptureFolderEnvironment(a_configured);
			}

			auto saveFolderName = F4SE::GetSaveFolderName();
			if (saveFolderName.empty()) {
				saveFolderName = "Fallout4";
			}

			wchar_t* knownBuffer = nullptr;
			const auto knownResult = REX::W32::SHGetKnownFolderPath(
				REX::W32::FOLDERID_Documents,
				REX::W32::KF_FLAG_DEFAULT,
				nullptr,
				std::addressof(knownBuffer));
			std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(
				knownBuffer, REX::W32::CoTaskMemFree);
			if (!knownPath || knownResult != 0) {
				L->warn("Failed to resolve the Documents folder for RenderDoc captures; using {}", kLegacyCaptureFolder);
				return kLegacyCaptureFolder;
			}

			std::filesystem::path path = knownPath.get();
			path /= std::format("My Games/{}/F4SE/FO4CommunityShaders/captures", saveFolderName);
			return path;
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
					a_error)
				&& AcceptSetting(
					feature_config::ReadString(*settingsTable, "capture_hotkey", a_candidate.captureHotkey),
					"capture_hotkey", "string", "string value is out of range", a_error)
				&& AcceptSetting(
					feature_config::ReadString(*settingsTable, "multi_capture_hotkey", a_candidate.multiCaptureHotkey),
					"multi_capture_hotkey", "string", "string value is out of range", a_error);
		}
	}

	bool RenderDoc::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		RefreshHotkeys();
		return true;
	}

	void RenderDoc::Load()
	{
		L->info("Settings: enabled={} dll={} folder={} min_free_disk_gib={:.2f} multi_frame_count={} capture={} multi_capture={}",
			_settings.enabled, _settings.dllPath, _settings.captureFolder,
			_settings.minFreeDiskGiB, _settings.multiFrameCount,
			_captureHotkey.ToString(), _multiCaptureHotkey.ToString());

		if (!_settings.enabled)
			return;
		// Load before D3D initialization.
		if (!TryLoadRuntime()) {
			FailLoad("RenderDoc runtime load failed for settings.dll_path '" + _settings.dllPath
				+ "'; verify the path and RenderDoc 1.7 API compatibility");
			return;
		}

		cs::Menu::Get().RegisterWndProcCallback(*this, &RenderDoc::HandleWndProc);
	}

	void RenderDoc::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("dll_path", _settings.dllPath);
		settings.insert_or_assign("capture_folder", _settings.captureFolder);
		settings.insert_or_assign("min_free_disk_gib", _settings.minFreeDiskGiB);
		settings.insert_or_assign("multi_frame_count", static_cast<int64_t>(_settings.multiFrameCount));
		settings.insert_or_assign("capture_hotkey", _settings.captureHotkey);
		settings.insert_or_assign("multi_capture_hotkey", _settings.multiCaptureHotkey);

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settings); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void RenderDoc::RefreshHotkeys()
	{
		bool ok = false;
		_captureHotkey = cs::input::Hotkey::Parse(_settings.captureHotkey, &ok);
		if (!ok)
			L->warn("Invalid capture_hotkey '{}', single-frame capture hotkey disabled", _settings.captureHotkey);

		ok = false;
		_multiCaptureHotkey = cs::input::Hotkey::Parse(_settings.multiCaptureHotkey, &ok);
		if (!ok)
			L->warn("Invalid multi_capture_hotkey '{}', multi-frame capture hotkey disabled", _settings.multiCaptureHotkey);

		if (_captureHotkey.IsBound() && _multiCaptureHotkey.IsBound()
			&& _captureHotkey.ToString() == _multiCaptureHotkey.ToString())
			L->warn("capture_hotkey and multi_capture_hotkey are both '{}'; multi-frame capture takes precedence",
				_multiCaptureHotkey.ToString());
	}

	bool RenderDoc::TryLoadRuntime()
	{
		if (_api)
			return true;
		if (_attemptedLoad)
			return false;
		_attemptedLoad = true;

		// Post-D3D loading can crash.
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

		_resolvedCaptureFolder = ResolveCaptureFolder(_settings.captureFolder);
		_resolvedCaptureFolderUtf8 = PathToUtf8(_resolvedCaptureFolder);
		L->info("RenderDoc capture folder: {}", _resolvedCaptureFolderUtf8);

		std::error_code ec;
		std::filesystem::create_directories(_resolvedCaptureFolder, ec);
		auto pathTemplate = PathToUtf8(_resolvedCaptureFolder / "FO4");
		_api->SetCaptureFilePathTemplate(pathTemplate.c_str());
	}

	bool RenderDoc::CheckCaptureDiskSpace() const
	{
		const auto& captureDir = _resolvedCaptureFolder;

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

	void RenderDoc::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("loaded", _api != nullptr)
			.Field("attempted", _attemptedLoad)
			.Field("captures", static_cast<std::int64_t>(_captureCount.load(std::memory_order_relaxed)))
			.Field("multi_frames", static_cast<std::int64_t>(_settings.multiFrameCount))
			.Field("folder", _resolvedCaptureFolderUtf8);
	}

	void RenderDoc::TriggerCapture()
	{
		if (!_settings.enabled) {
			L->warn("TriggerCapture called while feature disabled");
			return;
		}
		// Failed startup loads require a restart.
		if (!_api) {
			L->warn("RenderDoc runtime not loaded; restart the game with RenderDoc enabled to capture");
			return;
		}
		if (!CheckCaptureDiskSpace())
			return;

		_api->TriggerCapture();
		ApplyPendingComments();
		_captureCount.fetch_add(1, std::memory_order_relaxed);

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
		_captureCount.fetch_add(1, std::memory_order_relaxed);

		L->info("Multi-frame capture triggered: {} frames", frameCount);
	}

	bool RenderDoc::HandleWndProc(HWND, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
	{
		auto* self = GetSingleton();

		// Consume key-up early to prevent stuck input and F10 beeps.
		if (self->_captureReleaseVk != 0 && (a_msg == WM_KEYUP || a_msg == WM_SYSKEYUP)
			&& a_wparam == self->_captureReleaseVk) {
			self->_captureReleaseVk = 0;
			return true;
		}

		if (!self->_settings.enabled)
			return false;
		// Open menus leave keyboard input to ImGui.
		if (cs::Menu::Get().IsOpen())
			return false;

		// Multi-frame capture wins shared chords.
		if (self->_multiCaptureHotkey.MatchesDown(a_msg, a_wparam, a_lparam)) {
			self->TriggerMultiFrameCapture();
			self->_captureReleaseVk = self->_multiCaptureHotkey.vk;
			return true;
		}
		if (self->_captureHotkey.MatchesDown(a_msg, a_wparam, a_lparam)) {
			self->TriggerCapture();
			self->_captureReleaseVk = self->_captureHotkey.vk;
			return true;
		}
		return false;
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

		ImGui::TextDisabled("Restart after enabling.");
		ImGui::TextDisabled("%s captures one frame. %s captures the configured multi-frame count.",
			_captureHotkey.ToString().c_str(), _multiCaptureHotkey.ToString().c_str());

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
		RefreshHotkeys();
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
