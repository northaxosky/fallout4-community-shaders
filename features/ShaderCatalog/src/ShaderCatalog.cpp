#include "ShaderCatalog.h"

#include <Windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "CatalogDB.h"
#include "Hooks.h"
#include "Log.h"
#include "PixelShaderTracker.h"
#include "Plugin.h"
#include "Render/ShaderSubclassHooks.h"
#include "Settings/FeatureConfig.h"
#include "Sha1.h"
#include "SubclassAttribution.h"
#include "Telemetry/Telemetry.h"

#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.shadercatalog"); }

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

		// Catalog runtime families: OG=1.10.163; NG=1.10.980/1.10.984; AE=the 1.11.x line incl 1.11.221; engine_build_hash keeps exact builds distinct.
		const char* RuntimeLabel(const RuntimeVersion& v)
		{
			if (!v.valid) return "unknown";
			if (v.major == 1 && v.minor == 10) {
				if (v.build == 163) return "OG";
				if (v.build == 980) return "NG";
				if (v.build == 984) return "NG";
			}
			if (v.major == 1 && v.minor == 11)
				return "AE";  // Anniversary Edition line (e.g. 1.11.191, 1.11.221)
			return "unknown";
		}

		// Exact "major.minor.build" for the catalog's engine_build_hash column; empty if unreadable.
		std::string RuntimeBuildString(const RuntimeVersion& v)
		{
			if (!v.valid) return {};
			char buf[24];
			std::snprintf(buf, sizeof(buf), "%u.%u.%u", v.major, v.minor, v.build);
			return std::string(buf);
		}

		std::string PluginVersionString()
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%u.%u.%u",
				Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);
			return std::string(buf);
		}

		std::optional<std::string> WideToUtf8(const wchar_t* a_value)
		{
			if (!a_value || !*a_value)
				return std::nullopt;
			const int wideLength = static_cast<int>(wcsnlen_s(a_value, 128));
			const int required = WideCharToMultiByte(
				CP_UTF8, WC_ERR_INVALID_CHARS, a_value, wideLength,
				nullptr, 0, nullptr, nullptr);
			if (required <= 0)
				return std::nullopt;
			std::string result(static_cast<std::size_t>(required), '\0');
			if (WideCharToMultiByte(
					CP_UTF8, WC_ERR_INVALID_CHARS, a_value, wideLength,
					result.data(), required, nullptr, nullptr) != required)
				return std::nullopt;
			return result;
		}

		std::string FeatureLevelName(D3D_FEATURE_LEVEL a_level)
		{
			switch (a_level) {
			case D3D_FEATURE_LEVEL_9_1:
				return "9_1";
			case D3D_FEATURE_LEVEL_9_2:
				return "9_2";
			case D3D_FEATURE_LEVEL_9_3:
				return "9_3";
			case D3D_FEATURE_LEVEL_10_0:
				return "10_0";
			case D3D_FEATURE_LEVEL_10_1:
				return "10_1";
			case D3D_FEATURE_LEVEL_11_0:
				return "11_0";
			case D3D_FEATURE_LEVEL_11_1:
				return "11_1";
			default:
				return "unknown";
			}
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
		static auto* instance = new ShaderCatalog();
		return instance;
	}

	ShaderCatalog::~ShaderCatalog()
	{}

	void ShaderCatalog::FinalizeForProcessExit() noexcept
	{
		GetSingleton()->FinalizeOrderly();
	}

	void ShaderCatalog::FinalizeOrderly() noexcept
	{
		if (!_finalizerGate.TryBegin())
			return;
		try {
			if (!catalog::CatalogDB::Get().Stop())
				L->warn("Catalog process-exit finalization completed non-authoritatively.");
		} catch (const std::exception& e) {
			L->critical("Catalog process-exit finalization failed: {}", e.what());
		} catch (...) {
			L->critical("Catalog process-exit finalization failed: unknown exception");
		}
		catalog::shader_tracker::SetEnabled(false);
		_started.store(false, std::memory_order_release);
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
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("writer_flush_interval_ms", static_cast<int64_t>(_settings.writerFlushIntervalMs));
		settings.insert_or_assign("catalog_path", _settings.catalogPath);
		settings.insert_or_assign("subclass_attribution", _settings.subclassAttribution);

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settings); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void ShaderCatalog::Load()
	{
		if (!_settings.enabled) {
			L->info("Disabled by config; feature inert.");
			return;
		}

		catalog::Sha1InitOnce();

		const auto subclassLayout =
			cs::engine::GetShaderSubclassRuntimeLayout();
		const bool subclassAttributionEnabled =
			_settings.subclassAttribution && subclassLayout.verified;
		catalog::DbConfig dbc;
		dbc.catalogPath = _settings.catalogPath;
		dbc.flushIntervalMs =
			static_cast<std::uint32_t>(_settings.writerFlushIntervalMs);
		dbc.subclassAttributionRequested = _settings.subclassAttribution;
		dbc.subclassAttributionEnabled = subclassAttributionEnabled;

		const auto rtVersion = GetRuntimeVersion();
		const char* runtime = RuntimeLabel(rtVersion);
		const auto build = RuntimeBuildString(rtVersion);
		const auto version = PluginVersionString();
		catalog::RuntimeIdentity identity;
		identity.runtimeFamily = runtime;
		if (!build.empty())
			identity.runtimeVersion = build;
		identity.pluginVersion = version;
		identity.pluginBuildDescribe = CS_BUILD_DESCRIBE;
		identity.pluginGitIdentity = CS_BUILD_GIT_SHA;
		if (!catalog::CatalogDB::Get().Start(dbc, std::move(identity))) {
			catalog::shader_tracker::SetEnabled(false);
			_started.store(false, std::memory_order_release);
			FailLoad("Catalog database startup failed");
			L->error("Catalog database startup failed; feature inactive.");
			return;
		}
		const bool orderlyFinalizerReady =
			catalog::orderly_exit::Install(
				&ShaderCatalog::FinalizeForProcessExit)
			&& catalog::orderly_exit::IsInstalled()
			&& catalog::CatalogDB::Get().MarkOrderlyFinalizerReady();
		if (!orderlyFinalizerReady) {
			L->error(
				"ExitProcess finalizer hook failed; catalog run cannot be authoritative.");
		}

		catalog::hooks::SetSubclassAttributionEnabled(
			subclassAttributionEnabled);
		if (subclassAttributionEnabled) {
			if (!catalog::subclass_attribution::Register(
					subclassLayout.pixelShadersOffset)) {
				L->error(
					"Subclass attribution observer registration failed.");
			}
		} else {
			const char* why = !_settings.subclassAttribution
				? "disabled by config"
				: "unverified BSShader layout for this runtime";
			L->warn(
				"Catalog subclass attribution skipped ({}); "
				"shader injection remains active.",
				why);
		}
		catalog::shader_tracker::SetEnabled(true);
		_started.store(true, std::memory_order_release);
		L->info("Catalog initialized (runtime={})", runtime);
	}

	void ShaderCatalog::OnD3D11Ready(IDXGIAdapter* adapter, ID3D11Device* device)
	{
		if (!_started.load(std::memory_order_acquire) || !device)
			return;
		if (_hooksInstalled.load(std::memory_order_acquire))
			return;
		std::optional<std::string> adapterName;
		if (adapter) {
			DXGI_ADAPTER_DESC description{};
			if (SUCCEEDED(adapter->GetDesc(&description)))
				adapterName = WideToUtf8(description.Description);
		}
		catalog::CatalogDB::Get().SetGraphicsFacts(
			std::move(adapterName),
			FeatureLevelName(device->GetFeatureLevel()));
		bool expected = false;
		if (!_hookInstallInProgress.compare_exchange_strong(
				expected, true, std::memory_order_acq_rel))
			return;
		bool installed = false;
		try {
			installed = catalog::hooks::InstallAll(device);
		} catch (...) {
			_hookInstallInProgress.store(false, std::memory_order_release);
			throw;
		}
		_hooksInstalled.store(installed, std::memory_order_release);
		_hookInstallInProgress.store(false, std::memory_order_release);
		const auto hs = catalog::hooks::GetRuntimeAttributionStats();
		if (installed) {
			L->info("Device-vtable hooks installed (slots 12/13/14/15/16/17/18; PSSetShader={}).",
				hs.psSetShaderHookInstalled ? "yes" : "no");
		} else {
			L->error("Device-vtable hook coverage incomplete; run is non-authoritative.");
		}
	}

	void ShaderCatalog::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto stats = catalog::CatalogDB::Get().GetStats();
		const auto binds = catalog::hooks::GetRuntimeAttributionStats();
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("hooks", HooksInstalled())
			.Field("run_id", stats.generatedRunId)
			.Field("external_run_id", stats.externalRunId.value_or(""))
			.Field("scenario_id", stats.scenarioId.value_or(""))
			.Field("lifecycle", stats.lifecycle)
			.Field("authoritative", stats.authoritative)
			.Field("attempts", static_cast<std::int64_t>(stats.attempts))
			.Field("successes", static_cast<std::int64_t>(stats.successes))
			.Field("failures", static_cast<std::int64_t>(stats.failures))
			.Field("unique_observations", static_cast<std::int64_t>(stats.uniqueObservations))
			.Field("unique_contents", static_cast<std::int64_t>(stats.uniqueContents))
			.Field("attribution_events", static_cast<std::int64_t>(stats.attributionEvents))
			.Field("queue_overflow", static_cast<std::int64_t>(stats.quality.queueOverflow))
			.Field("malformed", static_cast<std::int64_t>(stats.quality.malformedBytecode))
			.Field("unsupported_size", static_cast<std::int64_t>(stats.quality.unsupportedSize))
			.Field("allocation_failure", static_cast<std::int64_t>(stats.quality.allocationFailure))
			.Field("hash_failure", static_cast<std::int64_t>(stats.quality.hashFailure))
			.Field("db_failure", static_cast<std::int64_t>(stats.quality.dbWriteFailure))
			.Field("raw_export_failure", static_cast<std::int64_t>(stats.quality.rawExportFailure))
			.Field("manifest_failure", static_cast<std::int64_t>(stats.quality.manifestFailure))
			.Field("raw_export_requested", stats.rawExportRequested)
			.Field("raw_export_complete", stats.rawExportComplete)
			.Field("writer_drained", stats.writerDrained)
			.Field("hook_coverage_ready", stats.hookCoverageReady)
			.Field(
				"orderly_finalizer_ready",
				stats.orderlyFinalizerReady)
			.Field("attributed_ps", static_cast<std::int64_t>(stats.attributedPs))
			.Field("total_ps", static_cast<std::int64_t>(stats.totalPs))
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
		ImGui::Text(
			"Generated run: %s",
			s.generatedRunId.empty() ? "inactive" : s.generatedRunId.c_str());
		ImGui::Text(
			"External run:  %s",
			s.externalRunId ? s.externalRunId->c_str() : "unknown");
		ImGui::Text(
			"Scenario:      %s",
			s.scenarioId ? s.scenarioId->c_str() : "unknown");
		ImGui::Text("Lifecycle:     %s", s.lifecycle.c_str());
		ImGui::Text("Authoritative: %s", s.authoritative ? "yes" : "no");
		ImGui::Text(
			"Hook coverage:  %s",
			s.hookCoverageReady ? "ready" : "incomplete");
		ImGui::Text(
			"Exit finalizer: %s",
			s.orderlyFinalizerReady ? "ready" : "unavailable");
		ImGui::Text(
			"Attempts:       %llu (%llu success, %llu failure)",
			static_cast<unsigned long long>(s.attempts),
			static_cast<unsigned long long>(s.successes),
			static_cast<unsigned long long>(s.failures));
		ImGui::Text(
			"Unique observations:   %llu",
			static_cast<unsigned long long>(s.uniqueObservations));
		ImGui::Text(
			"Unique contents:       %llu",
			static_cast<unsigned long long>(s.uniqueContents));
		ImGui::Text("Shapes enriched:       %llu", static_cast<unsigned long long>(s.reflected));
		ImGui::Text(
			"Attribution events:    %llu",
			static_cast<unsigned long long>(s.attributionEvents));
		ImGui::Text(
			"Dropped (queue):       %llu",
			static_cast<unsigned long long>(s.quality.queueOverflow));
		ImGui::Text(
			"Dropped (input/hash):  %llu",
			static_cast<unsigned long long>(
				s.quality.malformedBytecode + s.quality.unsupportedSize
				+ s.quality.allocationFailure + s.quality.copyFailure
				+ s.quality.hashFailure));
		ImGui::Text(
			"Raw export:             %s",
			!s.rawExportRequested ? "disabled"
				: (s.rawExportComplete ? "complete" : "incomplete"));
		if (s.totalPs > 0) {
			const auto pct =
				(100.0 * static_cast<double>(s.attributedPs))
				/ static_cast<double>(s.totalPs);
			ImGui::Text("Attributed PS rows:    %llu / %llu (%.1f%%)",
				static_cast<unsigned long long>(s.attributedPs),
				static_cast<unsigned long long>(s.totalPs), pct);
		} else {
			ImGui::Text("Attributed PS rows:    0 / 0");
		}
		const auto reloadHooks =
			cs::engine::GetReloadShaderHookInstallStats();
		ImGui::Text("ReloadShaders hooks:   %u/%u patched (%u failed)",
			reloadHooks.succeeded, reloadHooks.attempted, reloadHooks.failed);
		const auto setupHooks =
			cs::engine::GetSetupTechniqueHookInstallStats();
		ImGui::Text("SetupTechnique hooks:  %u/%u patched (%u failed)",
			setupHooks.succeeded, setupHooks.attempted, setupHooks.failed);
		const auto subclassRt =
			catalog::subclass_attribution::GetRuntimeStats();
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
