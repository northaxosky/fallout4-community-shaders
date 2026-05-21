#include "ShaderCatalog.h"

#include <Windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "CatalogDB.h"
#include "Hooks.h"
#include "Log.h"
#include "PixelShaderTracker.h"
#include "Plugin.h"
#include "Sha1.h"
#include "SimpleIni.h"
#include "SubclassHooks.h"

#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.catalog"); }

	constexpr const char* kIniPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ShaderCatalog.ini";

	namespace
	{
		// Detect FO4 runtime variant from the loaded Fallout4.exe file-version. Mapping:
		//   1.10.163  -> OG    (pre-Next-Gen)
		//   1.10.980+ -> NG    (Bethesda Next-Gen Update)
		//   1.11.*    -> AE    (newer patches; address-library uses AE bucket)
		// Anything outside these buckets defaults to OG; the catalog stores the runtime tag
		// verbatim and the importer can re-categorize if needed.
		const char* DetectRuntime()
		{
			HMODULE m = ::GetModuleHandleW(L"Fallout4.exe");
			if (!m) return "OG";
			wchar_t path[MAX_PATH] = {};
			if (!::GetModuleFileNameW(m, path, MAX_PATH)) return "OG";
			DWORD dummy = 0;
			const DWORD sz = ::GetFileVersionInfoSizeW(path, &dummy);
			if (!sz) return "OG";
			std::vector<unsigned char> buf(sz);
			if (!::GetFileVersionInfoW(path, 0, sz, buf.data())) return "OG";
			VS_FIXEDFILEINFO* fi = nullptr;
			UINT fiLen = 0;
			if (!::VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&fi), &fiLen) || !fi)
				return "OG";
			const auto major = HIWORD(fi->dwFileVersionMS);
			const auto minor = LOWORD(fi->dwFileVersionMS);
			const auto build = HIWORD(fi->dwFileVersionLS);
			if (major == 1 && minor == 11) return "AE";
			if (major == 1 && minor == 10 && build >= 980) return "NG";
			return "OG";
		}

		std::string PluginVersionString()
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%u.%u.%u",
				Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);
			return std::string(buf);
		}
	}

	ShaderCatalog* ShaderCatalog::GetSingleton()
	{
		static ShaderCatalog instance;
		return &instance;
	}

	ShaderCatalog::~ShaderCatalog()
	{
		// Best-effort teardown; DLL unload order may already have torn things down.
		catalog::shader_tracker::SetEnabled(false);
		catalog::CatalogDB::Get().Stop();
	}

	void ShaderCatalog::LoadSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		_settings.enabled               = ini.GetBoolValue("Settings", "bEnabled", _settings.enabled);
		_settings.writerFlushIntervalMs = static_cast<int>(ini.GetLongValue("Settings", "iWriterFlushIntervalMs", _settings.writerFlushIntervalMs));
		_settings.catalogPath           = ini.GetValue("Settings", "sCatalogPath", _settings.catalogPath.c_str());
		_settings.symbolicationBudgetUs = static_cast<int>(ini.GetLongValue("Settings", "iSymbolicationBudgetUs", _settings.symbolicationBudgetUs));

		// Clamp pathological values.
		if (_settings.writerFlushIntervalMs < 100)    _settings.writerFlushIntervalMs = 100;
		if (_settings.writerFlushIntervalMs > 60000)  _settings.writerFlushIntervalMs = 60000;
	}

	void ShaderCatalog::SaveSettings()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);

		ini.SetBoolValue("Settings", "bEnabled",               _settings.enabled);
		ini.SetLongValue("Settings", "iWriterFlushIntervalMs", _settings.writerFlushIntervalMs);
		ini.SetValue    ("Settings", "sCatalogPath",           _settings.catalogPath.c_str());
		ini.SetLongValue("Settings", "iSymbolicationBudgetUs", _settings.symbolicationBudgetUs);

		std::error_code ec;
		std::filesystem::create_directories(
			std::filesystem::path(kIniPath).parent_path(), ec);
		ini.SaveFile(kIniPath);
	}

	void ShaderCatalog::Load()
	{
		LoadSettings();
		if (!_settings.enabled) {
			L->info("Disabled by INI; feature inert.");
			return;
		}

		catalog::Sha1InitOnce();

		// Patch subclass reload/setup slots before D3D hooks see engine shader creation.
		catalog::subclass_hooks::InstallAll();

		catalog::DbConfig dbc;
		dbc.catalog_path      = _settings.catalogPath;
		dbc.flush_interval_ms = static_cast<std::uint32_t>(_settings.writerFlushIntervalMs);

		const auto runtime = DetectRuntime();
		const auto version = PluginVersionString();
		if (catalog::CatalogDB::Get().Start(dbc, runtime, version.c_str())) {
			catalog::shader_tracker::SetEnabled(true);
			_started.store(true, std::memory_order_release);
			L->info("Catalog initialized (runtime={})", runtime);
		} else {
			catalog::shader_tracker::SetEnabled(false);
			L->error("Catalog start failed; feature inert.");
			_started.store(false, std::memory_order_release);
		}
	}

	void ShaderCatalog::OnD3D11Ready(IDXGIAdapter* /*adapter*/, ID3D11Device* device)
	{
		if (!_started.load(std::memory_order_acquire) || !device)
			return;
		if (_hooksInstalled.exchange(true, std::memory_order_acq_rel))
			return;
		catalog::hooks::InstallAll(device);
		const auto hs = catalog::hooks::GetRuntimeAttributionStats();
		L->info("Device-vtable hooks installed (slots 12/13/15/16/17/18; PSSetShader={}).",
			hs.psSetShaderHookInstalled ? "yes" : "no");
	}

	void ShaderCatalog::DrawSettings()
	{
		const bool prev = _settings.enabled;
		if (ImGui::Checkbox("Enabled", &_settings.enabled)) {
			SaveSettings();
			if (_settings.enabled != prev)
				ImGui::OpenPopup("Restart required##ShaderCatalog");
		}
		ImGui::TextDisabled("Restart the game after toggling; device-vtable hooks install once at startup.");

		ImGui::Separator();
		ImGui::TextUnformatted("Stats");
		const auto s = catalog::CatalogDB::Get().GetStats();
		ImGui::Text("Shader hooks enqueued: %llu", static_cast<unsigned long long>(s.enqueued));
		ImGui::Text("Shader rows written:   %llu", static_cast<unsigned long long>(s.written));
		ImGui::Text("Attribution events:    %llu", static_cast<unsigned long long>(s.attribution_events));
		ImGui::Text("Dropped (ring full):   %llu", static_cast<unsigned long long>(s.dropped));
		if (s.total_ps > 0) {
			const auto pct = (100.0 * static_cast<double>(s.attributed_ps)) / static_cast<double>(s.total_ps);
			ImGui::Text("Attributed PS rows:    %llu / %llu (%.1f%%)",
				static_cast<unsigned long long>(s.attributed_ps),
				static_cast<unsigned long long>(s.total_ps), pct);
		} else {
			ImGui::Text("Attributed PS rows:    0 / 0");
		}
		const auto reloadHooks = catalog::subclass_hooks::GetReloadInstallStats();
		ImGui::Text("ReloadShaders hooks:   %u/%u patched (%u failed)",
			reloadHooks.succeeded, reloadHooks.attempted, reloadHooks.failed);
		const auto setupHooks = catalog::subclass_hooks::GetSetupTechniqueInstallStats();
		ImGui::Text("SetupTechnique hooks:  %u/%u patched (%u failed)",
			setupHooks.succeeded, setupHooks.attempted, setupHooks.failed);
		const auto subclassRt = catalog::subclass_hooks::GetRuntimeStats();
		ImGui::Text("SetupTechnique maps:   %llu / %llu attributed",
			static_cast<unsigned long long>(subclassRt.mapAttributions),
			static_cast<unsigned long long>(subclassRt.setupTechniqueCalls));
		const auto rt = catalog::hooks::GetRuntimeAttributionStats();
		ImGui::Text("PSSetShader hook:      %s", rt.psSetShaderHookInstalled ? "installed" : "not installed");
		ImGui::Text("Scoped PS binds:       %llu matched / %llu missed",
			static_cast<unsigned long long>(rt.matchedBinds),
			static_cast<unsigned long long>(rt.missedBinds));
		const auto tracker = catalog::shader_tracker::GetStats();
		ImGui::Text("Tracked PS pointers:   %llu (%llu aliases)",
			static_cast<unsigned long long>(tracker.tracked),
			static_cast<unsigned long long>(tracker.aliases));

		if (ImGui::Button("Open catalog folder")) {
			std::error_code ec;
			auto dir = std::filesystem::absolute(
				std::filesystem::path(_settings.catalogPath).parent_path(), ec).wstring();
			::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}

		if (ImGui::BeginPopupModal("Restart required##ShaderCatalog", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Enable/disable takes effect on next game launch.");
			if (ImGui::Button("OK", ImVec2(120, 0)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(ShaderCatalog::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
