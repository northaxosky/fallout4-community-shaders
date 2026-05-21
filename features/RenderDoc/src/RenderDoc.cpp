#include "RenderDoc.h"

#include <renderdoc_app.h>

#include <imgui.h>

#include <filesystem>

#include "Log.h"
#include "SimpleIni.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.renderdoc"); }

	constexpr const char* kIniPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\RenderDoc.ini";

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
			CSimpleIniA ini;
			ini.SetUnicode();
			ini.LoadFile("Data\\F4SE\\Plugins\\FO4CommunityShaders\\FrameGeneration.ini");
			const auto type = ini.GetLongValue("Settings", "iFrameGenType", 0);
			const auto fgEnabled = ini.GetBoolValue("Settings", "bFrameGenerationMode", true);
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
			        "Set bFrameGenerationMode=false or choose a non-DLSS-G backend before enabling RenderDoc.");
			_settings.enabled = false;
			return;
		}
		// Load before any D3D device exists; loading post-D3D-init is unreliable.
		TryLoadRuntime();
	}

	void RenderDoc::LoadSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		_settings.enabled       = ini.GetBoolValue("Settings", "bEnabled", _settings.enabled);
		_settings.dllPath       = ini.GetValue("Settings", "sDllPath", _settings.dllPath.c_str());
		_settings.captureFolder = ini.GetValue("Settings", "sCaptureFolder", _settings.captureFolder.c_str());

		L->info("Settings: enabled={} dll={} folder={}",
			_settings.enabled, _settings.dllPath, _settings.captureFolder);
	}

	void RenderDoc::SaveSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		ini.SetBoolValue("Settings", "bEnabled", _settings.enabled);
		ini.SetValue("Settings", "sDllPath", _settings.dllPath.c_str());
		ini.SetValue("Settings", "sCaptureFolder", _settings.captureFolder.c_str());

		ini.SaveFile(kIniPath);
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
