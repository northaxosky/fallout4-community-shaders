#include "ShaderReplacement.h"

#include <Windows.h>
#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <toml++/toml.hpp>

#include "Compiler.h"
#include "Hooks.h"
#include "Log.h"
#include "Registry.h"
#include "ShaderCatalog.h"
#include "Sha1.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.shaderreplacement"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ShaderReplacement.toml";
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
			if (name == "bsdf_light_deferred_point")       return &t.bsdf_light_deferred_point;
			if (name == "deferred_composite")              return &t.deferred_composite;
			if (name == "deferred_prepass")                return &t.deferred_prepass;
			if (name == "vls_slice_scatter")               return &t.vls_slice_scatter;
			return nullptr;
		}
	}

	ShaderReplacement* ShaderReplacement::GetSingleton()
	{
		static ShaderReplacement instance;
		return &instance;
	}

	bool ShaderReplacement::IsShaderEnabled(const std::string& a_name) const noexcept
	{
		auto& t = const_cast<PerShaderToggle&>(_settings.shaders);
		if (auto* p = TogglePtrFor(t, a_name)) return *p;
		return false;
	}

	void ShaderReplacement::LoadSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			return;
		}

		const auto settings = table["settings"];
		_settings.enabled = settings["enabled"].value_or(_settings.enabled);
		_settings.manifestPath = settings["manifest_path"].value_or(_settings.manifestPath);
		_settings.shadersRoot = settings["shaders_root"].value_or(_settings.shadersRoot);

		auto& s = _settings.shaders;
		s.ambient_ibl_pass = settings["replace_ambient_ibl_pass"].value_or(s.ambient_ibl_pass);
		s.bsdf_light_deferred_directional = settings["replace_bsdf_light_deferred_directional"].value_or(s.bsdf_light_deferred_directional);
		s.bsdf_light_deferred_point = settings["replace_bsdf_light_deferred_point"].value_or(s.bsdf_light_deferred_point);
		s.deferred_composite = settings["replace_deferred_composite"].value_or(s.deferred_composite);
		s.deferred_prepass = settings["replace_deferred_prepass"].value_or(s.deferred_prepass);
		s.vls_slice_scatter = settings["replace_vls_slice_scatter"].value_or(s.vls_slice_scatter);

		ApplyMarkerOverrides();
	}

	void ShaderReplacement::SaveSettings()
	{
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		auto& settings = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("manifest_path", _settings.manifestPath);
		settings.insert_or_assign("shaders_root", _settings.shadersRoot);

		auto& s = _settings.shaders;
		settings.insert_or_assign("replace_ambient_ibl_pass", s.ambient_ibl_pass);
		settings.insert_or_assign("replace_bsdf_light_deferred_directional", s.bsdf_light_deferred_directional);
		settings.insert_or_assign("replace_bsdf_light_deferred_point", s.bsdf_light_deferred_point);
		settings.insert_or_assign("replace_deferred_composite", s.deferred_composite);
		settings.insert_or_assign("replace_deferred_prepass", s.deferred_prepass);
		settings.insert_or_assign("replace_vls_slice_scatter", s.vls_slice_scatter);

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(), ec);
		std::ofstream out(kConfigPath);
		if (out) {
			out << table;
		}
	}

	void ShaderReplacement::ApplyMarkerOverrides()
	{
		// One-shot marker: smoke harness writes a tag string and overrides in-memory config.
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
		LoadSettings();
		if (!_settings.enabled) {
			L->info("Disabled by config; feature inert.");
			return;
		}

		replacement::Sha1InitOnce();

		const auto manifest = Widen(_settings.manifestPath);
		const auto root     = Widen(_settings.shadersRoot);
		if (!replacement::Registry::Get().LoadFromJson(manifest, root)) {
			L->error("Manifest load failed; feature inert.");
			return;
		}

		// Reconcile registry entries against per-shader config toggles.
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
		if (_hookInstalled.exchange(true, std::memory_order_acq_rel)) return;

		// Pre-compile enabled entries; entries without a known runtime sha1 are compiled too
		// so the ImGui surface can show compile status, but they cannot ever match at runtime.
		std::size_t want = 0, got = 0;
		for (auto& up : replacement::Registry::Get().All()) {
			auto& e = *up;
			if (!e.enabled_in_ini) continue;
			++want;
			if (replacement::CompileEntry(device, e)) ++got;
		}
		L->info("Compiled {}/{} replacements", got, want);

		const bool catalogHooked = ShaderCatalog::GetSingleton()->HooksInstalled();
		replacement::hooks::Install(device);
		L->info("Device-vtable hook installed (slot 15, catalog chain={}).", catalogHooked ? "present" : "absent");
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
