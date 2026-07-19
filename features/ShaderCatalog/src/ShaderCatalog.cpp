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
#include "Telemetry/Telemetry.h"

#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.shadercatalog"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ShaderCatalog.toml";

	namespace
	{
		struct RuntimeVersion
		{
			std::uint16_t major = 0;
			std::uint16_t minor = 0;
			std::uint16_t build = 0;
			bool          valid = false;
		};

		// Running Fallout4.exe file version (major.minor.build).
		RuntimeVersion GetRuntimeVersion()
		{
			RuntimeVersion v;
			HMODULE m = ::GetModuleHandleW(L"Fallout4.exe");
			if (!m) return v;
			wchar_t path[MAX_PATH] = {};
			if (!::GetModuleFileNameW(m, path, MAX_PATH)) return v;
			DWORD dummy = 0;
			const DWORD sz = ::GetFileVersionInfoSizeW(path, &dummy);
			if (!sz) return v;
			std::vector<unsigned char> buf(sz);
			if (!::GetFileVersionInfoW(path, 0, sz, buf.data())) return v;
			VS_FIXEDFILEINFO* fi = nullptr;
			UINT fiLen = 0;
			if (!::VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&fi), &fiLen) || !fi)
				return v;
			v.major = HIWORD(fi->dwFileVersionMS);
			v.minor = LOWORD(fi->dwFileVersionMS);
			v.build = HIWORD(fi->dwFileVersionLS);
			v.valid = true;
			return v;
		}

		// Catalog engine_runtime is constrained to OG/NG/AE: OG=1.10.163; NG=1.10.980/1.10.984; AE=the 1.11.x line (1.11.137..1.11.221).
		// engine_build_hash records the exact build so builds within a family remain distinguishable.
		const char* RuntimeLabel(const RuntimeVersion& v)
		{
			if (!v.valid) return "OG";
			if (v.major == 1 && v.minor == 10) {
				if (v.build == 163) return "OG";
				if (v.build == 980) return "NG";
				if (v.build == 984) return "NG";
			}
			if (v.major == 1 && v.minor == 11)
				return "AE";  // Anniversary Edition line (e.g. 1.11.191, 1.11.221)
			return "OG";
		}

		// Exact "major.minor.build" for the catalog's engine_build_hash column; empty if unreadable.
		std::string RuntimeBuildString(const RuntimeVersion& v)
		{
			if (!v.valid) return {};
			char buf[24];
			std::snprintf(buf, sizeof(buf), "%u.%u.%u", v.major, v.minor, v.build);
			return std::string(buf);
		}

		// BSShader::pixelShaders member offset: 0xB0 on 1.10.x (CommonLibF4); 0x128 on next-gen 1.11.x due to 3 inserted CRITICAL_SECTIONs (+0x78), confirmed vs fallout4-re 1.11.221 PDB.
		std::size_t PixelShadersOffset(const RuntimeVersion& v)
		{
			if (v.valid && v.major == 1 && v.minor == 11)
				return 0x128;
			return 0xB0;
		}

		bool RuntimeSupportsSubclassAttribution(const RuntimeVersion& v)
		{
			if (!v.valid || v.major != 1) return false;
			if (v.minor == 10 && (v.build == 163 || v.build == 980 || v.build == 984)) return true;
			if (v.minor == 11 && v.build == 221) return true;  // pixelShaders re-pointed to 0x128
			return false;
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
				&& AcceptSetting(
					feature_config::ReadBool(*settingsTable, "subclass_attribution", a_candidate.subclassAttribution),
					"subclass_attribution", "boolean", "boolean value is out of range", a_error);
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
		settings.insert_or_assign("subclass_attribution", _settings.subclassAttribution);

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

		const auto rtVersion = GetRuntimeVersion();
		const char* runtime = RuntimeLabel(rtVersion);
		const auto build = RuntimeBuildString(rtVersion);
		const auto version = PluginVersionString();
		if (!catalog::CatalogDB::Get().Start(dbc, runtime, version.c_str(), build.empty() ? nullptr : build.c_str())) {
			catalog::shader_tracker::SetEnabled(false);
			_started.store(false, std::memory_order_release);
			FailLoad("Catalog database startup failed");
			L->error("Catalog database startup failed; feature inactive.");
			return;
		}

		// Patch subclass reload/setup slots before D3D shader-creation hooks; unverified runtimes retain catalog function without subclass names.
		if (_settings.subclassAttribution && RuntimeSupportsSubclassAttribution(rtVersion)) {
			catalog::subclass_hooks::InstallAll(PixelShadersOffset(rtVersion));
		} else {
			const char* why = !_settings.subclassAttribution
				? "disabled by config"
				: "unverified BSShader layout for this runtime";
			L->warn("Subclass attribution skipped ({}); catalog + swap broker remain active.", why);
		}
		catalog::shader_tracker::SetEnabled(true);
		_started.store(true, std::memory_order_release);
		L->info("Catalog initialized (runtime={})", runtime);
	}

	void ShaderCatalog::RegisterPixelShaderSwapCallback(PixelShaderSwapCallback a_cb) noexcept
	{
		catalog::hooks::SetPixelShaderSwapCallback(a_cb);
	}

	void ShaderCatalog::RegisterPixelShaderBindObserver(PixelShaderBindObserver a_cb) noexcept
	{
		catalog::hooks::SetPixelShaderBindObserver(a_cb);
	}

	std::string ShaderCatalog::GetShaForPixelShader(ID3D11PixelShader* a_shader) const noexcept
	{
		catalog::Sha1Result sha{};
		if (!catalog::shader_tracker::TryGetPixelShaderSha1(a_shader, sha))
			return {};
		try {
			return catalog::Sha1ToHex(sha);
		} catch (...) {
			return {};
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

	void ShaderCatalog::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto stats = catalog::CatalogDB::Get().GetStats();
		const auto binds = catalog::hooks::GetRuntimeAttributionStats();
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("hooks", HooksInstalled())
			.Field("enqueued", static_cast<std::int64_t>(stats.enqueued))
			.Field("written", static_cast<std::int64_t>(stats.written))
			.Field("dropped", static_cast<std::int64_t>(stats.dropped))
			.Field("attributed_ps", static_cast<std::int64_t>(stats.attributed_ps))
			.Field("total_ps", static_cast<std::int64_t>(stats.total_ps))
			.Field("scoped", static_cast<std::int64_t>(binds.scopedBinds))
			.Field("matched", static_cast<std::int64_t>(binds.matchedBinds))
			.Field("missed", static_cast<std::int64_t>(binds.missedBinds));
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

		const bool prevAttr = _settings.subclassAttribution;
		if (ImGui::Checkbox("Subclass attribution", &_settings.subclassAttribution)) {
			SaveSettings();
			if (_settings.subclassAttribution != prevAttr)
				ImGui::OpenPopup("Restart required##ShaderCatalog");
		}
		ImGui::TextDisabled("Enriches PS rows with BSShader technique names. Auto-skipped on runtimes\nwith an unverified layout; the swap broker is unaffected.");

		ImGui::Separator();
		ImGui::TextUnformatted("Stats");
		const auto s = catalog::CatalogDB::Get().GetStats();
		ImGui::Text("Shader hooks enqueued: %llu", static_cast<unsigned long long>(s.enqueued));
		ImGui::Text("Shader rows written:   %llu", static_cast<unsigned long long>(s.written));
		ImGui::Text("Shapes enriched:       %llu", static_cast<unsigned long long>(s.reflected));
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
