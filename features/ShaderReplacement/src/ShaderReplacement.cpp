#include "ShaderReplacement.h"

#include <Windows.h>
#include <imgui.h>

#include <string>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Registry.h"
#include "Render/ShaderInjection.h"
#include "ScreenSpaceGI.h"
#include "ScreenSpaceShadows.h"
#include "Settings/FeatureConfig.h"
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

		bool DeveloperModeEnabled()
		{
			const auto root = feature_config::GetMergedRoot();
			const auto* menu = root["menu"].as_table();
			if (!menu)
				return false;

			bool enabled = false;
			return feature_config::ReadBool(*menu, "developer_mode", enabled)
					== feature_config::ScalarReadStatus::kValid
				&& enabled;
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
		const auto* target = cs::engine::FindShaderInjectionTarget(a_name);
		return target ? cs::engine::GetInjectedPixelShader(target->id) : nullptr;
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

		const auto manifest = Widen(_settings.manifestPath);
		if (!replacement::Registry::Get().LoadFromJson(manifest)) {
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

		_developerForceOffActive = DeveloperModeEnabled();
		(void)cs::engine::SetDeveloperShaderForceOffEnabled(_developerForceOffActive);
		(void)cs::engine::SetDeveloperShaderSourceRoot(Widen(_settings.shadersRoot));
		for (const auto& target : cs::engine::GetShaderInjectionTargets()) {
			const bool forceOn = IsShaderEnabled(std::string(target.name));
			const auto shaderOverride = forceOn
				? cs::engine::DeveloperShaderOverride::kForceOn
				: cs::engine::DeveloperShaderOverride::kForceOff;
			(void)cs::engine::SetDeveloperShaderOverride(target.id, shaderOverride);
		}

		const auto registerDirectionalDefine = [this](cs::engine::ShaderInjectionTarget a_target) {
			(void)cs::engine::RegisterReplacement({
				.targetId = a_target,
				.contributor = "ShaderReplacement legacy ScreenSpaceShadows define",
				.defines = { { "SCREEN_SPACE_SHADOWS", "1" } },
				.isReady = [this, a_target] {
					const auto* target = cs::engine::GetShaderInjectionTarget(a_target);
					return target
						&& IsShaderEnabled(std::string(target->name))
						&& cs::features::ScreenSpaceShadows::GetSingleton()->IsShadowMaskReady();
				}
			});
		};
		// TODO(inject-step5): ScreenSpaceShadows registers these contributors.
		registerDirectionalDefine(cs::engine::ShaderInjectionTarget::kBsdfLightDeferredDirectional);
		registerDirectionalDefine(cs::engine::ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl);

		(void)cs::engine::RegisterReplacement({
			.targetId = cs::engine::ShaderInjectionTarget::kAmbientIblPass,
			.contributor = "ShaderReplacement legacy ScreenSpaceGI define",
			.defines = { { "SSGI", "1" } },
			.isReady = [this] {
				return IsShaderEnabled("ambient_ibl_pass")
					&& cs::features::ScreenSpaceGI::GetSingleton()->IsReady();
			}
		});

		if (!_developerForceOffActive) {
			L->info("Developer mode is off; force-off overrides are treated as Auto.");
		}
		_started.store(true, std::memory_order_release);
	}

	void ShaderReplacement::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto summary = cs::engine::GetShaderInjectionSummary();
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("started", _started.load(std::memory_order_acquire))
			.Field("compiled", static_cast<std::int64_t>(summary.compiled))
			.Field("requested", static_cast<std::int64_t>(summary.requested))
			.Field("substitutions", static_cast<std::int64_t>(summary.substitutions))
			.Field("matches", static_cast<std::int64_t>(summary.matches));
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
					const auto* target = cs::engine::FindShaderInjectionTarget(e.name);
					const auto status = target
						? cs::engine::GetShaderInjectionTargetSnapshot(target->id)
						: cs::engine::ShaderInjectionTargetSnapshot{};
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.name.c_str());
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(e.enabled_in_ini ? "yes" : "no");
					ImGui::TableSetColumnIndex(2);
					if (!target)                         ImGui::TextDisabled("unsupported");
					else if (!status.compileAttempted)   ImGui::TextDisabled("skipped");
					else if (status.compileOk)           ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1), "ok");
					else                                 ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1), "FAIL");
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%llu", static_cast<unsigned long long>(status.matches));
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%llu", static_cast<unsigned long long>(status.substitutions));
					if (!status.compileOk && !status.compileError.empty()) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextWrapped("    %s", status.compileError.c_str());
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
