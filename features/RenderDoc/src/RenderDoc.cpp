#include "RenderDoc.h"

#include <renderdoc_app.h>

#include <imgui.h>
#include <toml++/toml.hpp>

#include <filesystem>
#include <fstream>

#include "Log.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.renderdoc"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\RenderDoc.toml";

	RenderDoc* RenderDoc::GetSingleton()
	{
		static RenderDoc singleton;
		return &singleton;
	}

	namespace
	{
		// RenderDoc detours D3D11/DXGI early; Streamline's DLSS-G feature functions then fail to resolve
		// and the game CTDs when Upscaling tries to fall back mid-flight. Refuse to load if DLSS-G is on.
		bool DLSSGRequested()
		{
			toml::table table;
			try {
				table = toml::parse_file("Data\\F4SE\\Plugins\\FO4CommunityShaders\\FrameGeneration.toml");
			} catch (const toml::parse_error&) {
				return false;
			}
			const auto type = table["settings"]["frame_gen_type"].value_or<int64_t>(0);
			const auto fgEnabled = table["settings"]["frame_generation_mode"].value_or(true);
			return fgEnabled && type == 1;
		}
	}

	void RenderDoc::Load()
	{
		LoadSettings();
		if (!_settings.enabled)
			return;
		if (DLSSGRequested()) {
			L->warn("RenderDoc disabled at load: incompatible with DLSS-G frame generation. "
			        "Set frame_generation_mode=false or choose a non-DLSS-G backend before enabling RenderDoc.");
			_settings.enabled = false;
			return;
		}
		// Load before any D3D device exists; loading post-D3D-init is unreliable.
		TryLoadRuntime();
	}

	void RenderDoc::LoadSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			return;
		}

		_settings.enabled       = table["settings"]["enabled"].value_or(_settings.enabled);
		_settings.dllPath       = table["settings"]["dll_path"].value_or(_settings.dllPath);
		_settings.captureFolder = table["settings"]["capture_folder"].value_or(_settings.captureFolder);

		L->info("Settings: enabled={} dll={} folder={}",
			_settings.enabled, _settings.dllPath, _settings.captureFolder);
	}

	void RenderDoc::SaveSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		auto& settings = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("dll_path", _settings.dllPath);
		settings.insert_or_assign("capture_folder", _settings.captureFolder);

		std::ofstream out(kConfigPath);
		if (out) {
			out << table;
		}
	}

	bool RenderDoc::TryLoadRuntime()
	{
		if (_api)
			return true;
		if (_attemptedLoad)
			return false;
		_attemptedLoad = true;

		// Loading renderdoc.dll into a process that already has D3D devices created can crash;
		// callers should ensure this happens before D3D init (Load / OnPostPostLoad timing).
		_module = LoadLibraryA(_settings.dllPath.c_str());
		if (!_module) {
			L->warn("LoadLibrary({}) failed: {:#x}", _settings.dllPath, GetLastError());
			return false;
		}

		auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(_module, "RENDERDOC_GetAPI"));
		if (!getApi) {
			L->warn("RENDERDOC_GetAPI not found in {}", _settings.dllPath);
			return false;
		}

		if (getApi(eRENDERDOC_API_Version_1_7_0, reinterpret_cast<void**>(&_api)) != 1 || !_api) {
			L->warn("RENDERDOC_GetAPI returned no API for version 1.7.0");
			_api = nullptr;
			return false;
		}

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

	void RenderDoc::TriggerCapture()
	{
		if (!_settings.enabled) {
			L->warn("TriggerCapture called while feature disabled");
			return;
		}
		if (!TryLoadRuntime())
			return;

		_api->TriggerCapture();

		// Empty path binds comments to the most-recent capture (the one we just triggered).
		// Buffer cleared so notes don't bleed across captures.
		if (_commentsBuf[0]) {
			_api->SetCaptureFileComments("", _commentsBuf.data());
			_commentsBuf[0] = 0;
		}

		L->info("Capture triggered");
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

		ImGui::InputTextMultiline("Comments (embedded in next .rdc)",
			_commentsBuf.data(), _commentsBuf.size(),
			ImVec2(0, ImGui::GetTextLineHeight() * 3));

		ImGui::BeginDisabled(!_api);
		if (ImGui::Button("Trigger Capture"))
			TriggerCapture();
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
