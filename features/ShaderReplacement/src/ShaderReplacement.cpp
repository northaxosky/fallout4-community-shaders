#include "ShaderReplacement.h"

#include <Windows.h>
#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <toml++/toml.hpp>

#include "Compiler.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Registry.h"
#include "ScreenSpaceGI.h"
#include "ScreenSpaceShadows.h"
#include "Settings/FeatureConfig.h"
#include "ShaderCatalog.h"
#include "Sha1.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.shaderreplacement"); }

	constexpr const char* kMarkerPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.shaderreplace_force";

	namespace
	{
		std::wstring Widen(const std::string& s)
		{
			std::wstring w;
			w.reserve(s.size());
			for (char c : s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
			return w;
		}

		bool* TogglePtrFor(ShaderReplacement::PerShaderToggle& t, const std::string& name)
		{
			if (name == "ambient_ibl_pass")                return &t.ambient_ibl_pass;
			if (name == "bsdf_light_deferred_directional") return &t.bsdf_light_deferred_directional;
			if (name == "bsdf_light_deferred_directional_ibl") return &t.bsdf_light_deferred_directional_ibl;
			if (name == "bsdf_light_deferred_point")       return &t.bsdf_light_deferred_point;
			if (name == "deferred_composite")              return &t.deferred_composite;
			if (name == "deferred_prepass")                return &t.deferred_prepass;
			if (name == "vls_slice_scatter")               return &t.vls_slice_scatter;
			return nullptr;
		}

		std::string SettingError(std::string_view a_key, std::string_view a_reason)
		{
			return "settings." + std::string(a_key) + ": " + std::string(a_reason);
		}

		bool AcceptSetting(
			feature_config::ScalarReadStatus a_status,
			std::string_view a_key,
			std::string_view a_expected,
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
				a_error = SettingError(a_key, "value is out of range");
				break;
			}
			return false;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			ShaderReplacement::Settings& a_candidate,
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

			auto& shaders = a_candidate.shaders;
			return AcceptSetting(
					feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled),
					"enabled", "boolean", a_error)
				&& AcceptSetting(
					feature_config::ReadString(*settingsTable, "manifest_path", a_candidate.manifestPath),
					"manifest_path", "string", a_error)
				&& AcceptSetting(
					feature_config::ReadString(*settingsTable, "shaders_root", a_candidate.shadersRoot),
					"shaders_root", "string", a_error)
				&& AcceptSetting(
					feature_config::ReadBool(*settingsTable, "replace_ambient_ibl_pass", shaders.ambient_ibl_pass),
					"replace_ambient_ibl_pass", "boolean", a_error)
				&& AcceptSetting(
					feature_config::ReadBool(*settingsTable, "replace_bsdf_light_deferred_directional", shaders.bsdf_light_deferred_directional),
					"replace_bsdf_light_deferred_directional", "boolean", a_error)
				&& AcceptSetting(
					feature_config::ReadBool(*settingsTable, "replace_bsdf_light_deferred_directional_ibl", shaders.bsdf_light_deferred_directional_ibl),
					"replace_bsdf_light_deferred_directional_ibl", "boolean", a_error)
				&& AcceptSetting(
					feature_config::ReadBool(*settingsTable, "replace_bsdf_light_deferred_point", shaders.bsdf_light_deferred_point),
					"replace_bsdf_light_deferred_point", "boolean", a_error)
				&& AcceptSetting(
					feature_config::ReadBool(*settingsTable, "replace_deferred_composite", shaders.deferred_composite),
					"replace_deferred_composite", "boolean", a_error)
				&& AcceptSetting(
					feature_config::ReadBool(*settingsTable, "replace_deferred_prepass", shaders.deferred_prepass),
					"replace_deferred_prepass", "boolean", a_error)
				&& AcceptSetting(
					feature_config::ReadBool(*settingsTable, "replace_vls_slice_scatter", shaders.vls_slice_scatter),
					"replace_vls_slice_scatter", "boolean", a_error);
		}
	}

	ShaderReplacement* ShaderReplacement::GetSingleton()
	{
		static ShaderReplacement instance;
		return &instance;
	}

	ID3D11PixelShader* ShaderReplacement::GetReplacementPixelShader(std::string_view a_name) const noexcept
	{
		auto* entry = replacement::Registry::Get().FindByName(a_name);
		return entry ? entry->compiled_ps.get() : nullptr;
	}

	void ShaderReplacement::RegisterInjection(
		std::string a_passName,
		InjectionCallback a_callback)
	{
		if (a_passName.empty() || !a_callback) {
			return;
		}
		_injections.push_back({ std::move(a_passName), std::move(a_callback) });
	}

	void ShaderReplacement::DispatchInjections(
		std::string_view a_passName,
		ID3D11DeviceContext* a_context) noexcept
	{
		auto* entry = replacement::Registry::Get().FindByName(a_passName);
		if (!a_context ||
			!_settings.enabled ||
			!entry ||
			!entry->enabled_in_ini ||
			!entry->compile_ok ||
			!entry->compiled_ps ||
			entry->substitution_hits.load(std::memory_order_relaxed) == 0) {
			return;
		}

		for (const auto& injection : _injections) {
			if (injection.passName != a_passName) {
				continue;
			}
			try {
				injection.callback(a_context);
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::warn,
					"Shader injection for '{}' failed: {}.",
					entry->name,
					e.what());
			} catch (...) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::warn,
					"Shader injection for '{}' failed.",
					entry->name);
			}
		}
	}

	bool ShaderReplacement::IsShaderEnabled(const std::string& a_name) const noexcept
	{
		auto& t = const_cast<PerShaderToggle&>(_settings.shaders);
		if (auto* p = TogglePtrFor(t, a_name)) return *p;
		return false;
	}

	bool ShaderReplacement::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		return true;
	}

	void ShaderReplacement::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("manifest_path", _settings.manifestPath);
		settings.insert_or_assign("shaders_root", _settings.shadersRoot);

		auto& s = _settings.shaders;
		settings.insert_or_assign("replace_ambient_ibl_pass", s.ambient_ibl_pass);
		settings.insert_or_assign("replace_bsdf_light_deferred_directional", s.bsdf_light_deferred_directional);
		settings.insert_or_assign("replace_bsdf_light_deferred_directional_ibl", s.bsdf_light_deferred_directional_ibl);
		settings.insert_or_assign("replace_bsdf_light_deferred_point", s.bsdf_light_deferred_point);
		settings.insert_or_assign("replace_deferred_composite", s.deferred_composite);
		settings.insert_or_assign("replace_deferred_prepass", s.deferred_prepass);
		settings.insert_or_assign("replace_vls_slice_scatter", s.vls_slice_scatter);

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settings); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void ShaderReplacement::ApplyMarkerOverrides()
	{
		// One-shot smoke marker: tag string overrides in-memory config.
		FILE* f = nullptr;
		if (fopen_s(&f, kMarkerPath, "r") != 0 || !f) return;
		char buf[64] = {};
		const auto n = std::fread(buf, 1, sizeof(buf) - 1, f);
		std::fclose(f);
		std::string tag(buf, buf + n);
		while (!tag.empty() && (tag.back() == '\n' || tag.back() == '\r' || tag.back() == ' ' || tag.back() == '\t'))
			tag.pop_back();
		if (tag.empty()) return;

		auto& s = _settings.shaders;
		s = PerShaderToggle{};
		bool any = true;
		if      (tag == "none")        { _settings.enabled = false; any = false; }
		else if (tag == "all")         { _settings.enabled = true; s.ambient_ibl_pass = s.bsdf_light_deferred_directional =
		                                  s.bsdf_light_deferred_point = s.deferred_composite = s.deferred_prepass = s.vls_slice_scatter = true; }
		else if (tag == "composite")   { _settings.enabled = true; s.deferred_composite = true; }
		else if (tag == "ambient")     { _settings.enabled = true; s.ambient_ibl_pass = true; }
		else if (tag == "prepass")     { _settings.enabled = true; s.deferred_prepass = true; }
		else if (tag == "bsdf-dir")    { _settings.enabled = true; s.bsdf_light_deferred_directional = true; }
		else if (tag == "bsdf-pt")     { _settings.enabled = true; s.bsdf_light_deferred_point = true; }
		else if (tag == "vls")         { _settings.enabled = true; s.vls_slice_scatter = true; }
		else { L->warn("Unknown marker tag '{}'; ignoring.", tag); return; }

		L->info("Marker override applied (tag={}, master={}).", tag, _settings.enabled ? "on" : "off");
		(void)any;
	}

	void ShaderReplacement::Load()
	{
		ApplyMarkerOverrides();
		if (!_settings.enabled) {
			L->info("Disabled by config; feature inert.");
			return;
		}

		replacement::Sha1InitOnce();

		const auto manifest = Widen(_settings.manifestPath);
		const auto root     = Widen(_settings.shadersRoot);
		if (!replacement::Registry::Get().LoadFromJson(manifest, root)) {
			FailLoad("Shader replacement manifest failed to load");
			L->error("Manifest load failed; feature inactive.");
			return;
		}

		auto& s = _settings.shaders;
		for (auto& up : replacement::Registry::Get().All()) {
			auto& e = *up;
			if (auto* p = TogglePtrFor(s, e.name))
				e.enabled_in_ini = *p;
			else
				e.enabled_in_ini = e.default_enabled;
		}

		_started.store(true, std::memory_order_release);
	}

	void ShaderReplacement::OnD3D11Ready(IDXGIAdapter* /*adapter*/, ID3D11Device* device)
	{
		if (!_started.load(std::memory_order_acquire) || !device) return;

		// Compile SHA1-less entries for ImGui status; they can never match runtime bytecode.
		std::size_t want = 0, got = 0;
		for (auto& up : replacement::Registry::Get().All()) {
			auto& e = *up;
			if (!e.enabled_in_ini) continue;
			++want;
			if ((e.name == "bsdf_light_deferred_directional" ||
					e.name == "bsdf_light_deferred_directional_ibl") &&
				cs::features::ScreenSpaceShadows::GetSingleton()->IsShadowMaskReady()) {
				// Interim ShaderReplacement<->SSS coupling: define SCREEN_SPACE_SHADOWS only when SSS loaded and mask-ready; registry will replace this.
				e.defines.emplace_back("SCREEN_SPACE_SHADOWS", "1");
				L->info("Enabled SCREEN_SPACE_SHADOWS for '{}'.", e.name);
			}
			if (e.name == "ambient_ibl_pass" &&
				cs::features::ScreenSpaceGI::GetSingleton()->IsReady()) {
				e.defines.emplace_back("SSGI", "1");
				L->info("Enabled SSGI for '{}'.", e.name);
			}
			if (replacement::CompileEntry(device, e)) ++got;
		}
		L->info("Compiled {}/{} replacements", got, want);
		_compiledWant = want;
		_compiledGot  = got;

		// Use ShaderCatalog's CreatePixelShader vtable slot-15 hook; one detour owns PS swaps.
		ShaderCatalog::GetSingleton()->RegisterPixelShaderSwapCallback(
			[](const void* /*a_bytecode*/, std::size_t /*a_bytecode_len*/,
				const cs::features::catalog::Sha1Result& a_sha, ID3D11PixelShader** a_out) -> bool {
				// Only runtime-SHA1 matches may replace; false returns keep byte-identical/OFF-path parity.
				auto* entry = replacement::Registry::Get().FindByRuntimeSha1(a_sha);
				if (!entry)
					return false;
				entry->match_hits.fetch_add(1, std::memory_order_relaxed);
				if (!entry->enabled_in_ini) {
					entry->passthrough_disabled.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
				if (!entry->compile_ok || !entry->compiled_ps) {
					entry->passthrough_compile_fail.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
				// Swap *a_out with net refcount 1: AddRef ours, Release engine's.
				ID3D11PixelShader* mine = entry->compiled_ps.get();
				mine->AddRef();
				(*a_out)->Release();
				*a_out = mine;
				const auto prev = entry->substitution_hits.fetch_add(1, std::memory_order_relaxed);
				if (prev == 0)
					L->info("Replaced PS sha={} -> {}", replacement::Sha1ToHex(a_sha), entry->name);
				return true;
			});
		const bool catalogHooked = ShaderCatalog::GetSingleton()->HooksInstalled();
		L->info("Registered pixel-shader swap callback (catalog hook={}).", catalogHooked ? "present" : "absent");
	}

	void ShaderReplacement::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		std::uint64_t subs = 0, matches = 0;
		for (const auto& up : replacement::Registry::Get().All()) {
			subs    += up->substitution_hits.load(std::memory_order_relaxed);
			matches += up->match_hits.load(std::memory_order_relaxed);
		}
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("started", _started.load(std::memory_order_acquire))
			.Field("compiled", static_cast<std::int64_t>(_compiledGot))
			.Field("requested", static_cast<std::int64_t>(_compiledWant))
			.Field("substitutions", static_cast<std::int64_t>(subs))
			.Field("matches", static_cast<std::int64_t>(matches));
	}

	void ShaderReplacement::DrawSettings()
	{
		const bool prev = _settings.enabled;
		if (ImGui::Checkbox("Enabled", &_settings.enabled)) {
			SaveSettings();
			if (_settings.enabled != prev)
				ImGui::OpenPopup("Restart required##ShaderReplacement");
		}
		ImGui::TextDisabled("Per-shader checkboxes take effect on next launch (compile + hook install at startup).");

		ImGui::Separator();
		auto& s = _settings.shaders;
		bool dirty = false;
		dirty |= ImGui::Checkbox("ambient_ibl_pass",                &s.ambient_ibl_pass);
		dirty |= ImGui::Checkbox("bsdf_light_deferred_directional", &s.bsdf_light_deferred_directional);
		dirty |= ImGui::Checkbox("bsdf_light_deferred_directional_ibl", &s.bsdf_light_deferred_directional_ibl);
		dirty |= ImGui::Checkbox("bsdf_light_deferred_point",       &s.bsdf_light_deferred_point);
		dirty |= ImGui::Checkbox("deferred_composite",              &s.deferred_composite);
		dirty |= ImGui::Checkbox("deferred_prepass",                &s.deferred_prepass);
		dirty |= ImGui::Checkbox("vls_slice_scatter",               &s.vls_slice_scatter);
		if (dirty) {
			SaveSettings();
			ImGui::OpenPopup("Restart required##ShaderReplacement");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Status");
		if (!replacement::Registry::Get().Loaded()) {
			ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Manifest not loaded: %s",
				replacement::Registry::Get().LastError().c_str());
		} else {
			if (ImGui::BeginTable("##replacements", 5,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("name");
				ImGui::TableSetupColumn("enabled");
				ImGui::TableSetupColumn("compile");
				ImGui::TableSetupColumn("matches");
				ImGui::TableSetupColumn("substituted");
				ImGui::TableHeadersRow();
				for (auto& up : replacement::Registry::Get().All()) {
					const auto& e = *up;
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.name.c_str());
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(e.enabled_in_ini ? "yes" : "no");
					ImGui::TableSetColumnIndex(2);
					if (!e.compile_attempted)      ImGui::TextDisabled("skipped");
					else if (e.compile_ok)         ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1), "ok");
					else                           ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1), "FAIL");
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%llu", static_cast<unsigned long long>(e.match_hits.load(std::memory_order_relaxed)));
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%llu", static_cast<unsigned long long>(e.substitution_hits.load(std::memory_order_relaxed)));
					if (!e.compile_ok && !e.compile_error.empty()) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextWrapped("    %s", e.compile_error.c_str());
					}
				}
				ImGui::EndTable();
			}
		}

		if (ImGui::BeginPopupModal("Restart required##ShaderReplacement", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Toggle takes effect on next game launch (compile + vtable hook install at startup).");
			if (ImGui::Button("OK", ImVec2(120, 0)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(ShaderReplacement::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
