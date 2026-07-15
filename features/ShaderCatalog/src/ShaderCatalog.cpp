#include "ShaderCatalog.h"

#include <Windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "CatalogDB.h"
#include "Hooks.h"
#include "Log.h"
#include "PixelShaderTracker.h"
#include "Plugin.h"
#include "Settings/FeatureConfig.h"
#include "Sha1.h"
#include "SubclassHooks.h"

#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.catalog"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ShaderCatalog.toml";

	namespace
	{
		// Runtime tags: 1.10.163=OG, 1.10.980=AE, 1.10.984=NG; unknown builds default to OG.
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
			if (major == 1 && minor == 10) {
				if (build == 163) return "OG";
				if (build == 980) return "AE";
				if (build == 984) return "NG";
			}
			return "OG";
		}

		std::string PluginVersionString()
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%u.%u.%u",
				Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);
			return std::string(buf);
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
				a_error = SettingError(a_key, "invalid value");
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
			std::string_view a_range,
			int& a_value,
			std::string& a_error)
		{
			auto value = static_cast<std::int64_t>(a_value);
			const auto status = feature_config::ReadSignedInteger(a_table, a_key, value, a_min, a_max);
			if (!AcceptSetting(status, a_key, "integer", a_range, a_error)) {
				return false;
			}
			if (status == feature_config::ScalarReadStatus::kValid) {
				a_value = static_cast<int>(value);
			}
			return true;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			ShaderCatalog::Settings& a_candidate,
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
				&& ReadIntegerSetting(
					*settingsTable,
					"writer_flush_interval_ms",
					100,
					60000,
					"value must be in range 100..60000",
					a_candidate.writerFlushIntervalMs,
					a_error)
				&& AcceptSetting(
					feature_config::ReadString(*settingsTable, "catalog_path", a_candidate.catalogPath),
					"catalog_path", "string", "string value is out of range", a_error)
				&& ReadIntegerSetting(
					*settingsTable,
					"symbolication_budget_us",
					std::numeric_limits<int>::min(),
					std::numeric_limits<int>::max(),
					"value must be representable as int",
					a_candidate.symbolicationBudgetUs,
					a_error);
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

	bool ShaderCatalog::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		return true;
	}

	std::optional<bool> ShaderCatalog::GetLegacyActivationIntent(const toml::table& a_config) const
	{
		const auto* settingsNode = a_config.get("settings");
		const auto* settingsTable = settingsNode ? settingsNode->as_table() : nullptr;
		const auto* enabledNode = settingsTable ? settingsTable->get("enabled") : nullptr;
		if (!enabledNode || !enabledNode->is_boolean()) {
			return std::nullopt;
		}
		return enabledNode->as_boolean()->get();
	}

	bool ShaderCatalog::HasCapability(FeatureCapability a_capability) const noexcept
	{
		return a_capability == FeatureCapability::kPixelShaderSwapBroker
			&& _started.load(std::memory_order_acquire);
	}

	void ShaderCatalog::SaveSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		auto& settings = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("writer_flush_interval_ms", static_cast<int64_t>(_settings.writerFlushIntervalMs));
		settings.insert_or_assign("catalog_path", _settings.catalogPath);
		settings.insert_or_assign("symbolication_budget_us", static_cast<int64_t>(_settings.symbolicationBudgetUs));

		std::error_code ec;
		std::filesystem::create_directories(
			std::filesystem::path(kConfigPath).parent_path(), ec);
		std::ofstream out(kConfigPath);
		if (out) {
			out << table;
		}
	}

	void ShaderCatalog::Load()
	{
		if (!_settings.enabled) {
			L->info("Disabled by config; feature inert.");
			return;
		}

		catalog::Sha1InitOnce();

		catalog::DbConfig dbc;
		dbc.catalog_path      = _settings.catalogPath;
		dbc.flush_interval_ms = static_cast<std::uint32_t>(_settings.writerFlushIntervalMs);

		const auto runtime = DetectRuntime();
		const auto version = PluginVersionString();
		if (!catalog::CatalogDB::Get().Start(dbc, runtime, version.c_str())) {
			catalog::shader_tracker::SetEnabled(false);
			_started.store(false, std::memory_order_release);
			FailLoad("Catalog database startup failed");
			L->error("Catalog database startup failed; feature inactive.");
			return;
		}

		// Patch subclass reload/setup slots before D3D shader-creation hooks run.
		catalog::subclass_hooks::InstallAll();
		catalog::shader_tracker::SetEnabled(true);
		_started.store(true, std::memory_order_release);
		L->info("Catalog initialized (runtime={})", runtime);
	}

	void ShaderCatalog::RegisterPixelShaderSwapCallback(PixelShaderSwapCallback a_cb) noexcept
	{
		catalog::hooks::SetPixelShaderSwapCallback(a_cb);
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
