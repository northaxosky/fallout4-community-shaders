#include "PresetManager.h"

#include "ImagespaceConfigIO.h"
#include "Log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <toml++/toml.hpp>

namespace
{
	auto* L = cs::log::Get("cs.feature.imagespace");

	constexpr std::string_view kPresetsRoot    = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Presets";
	constexpr std::string_view kPresetsBuiltin = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\Presets\\Builtin";

	std::string ToLower(std::string_view a_in)
	{
		std::string out;
		out.reserve(a_in.size());
		for (char c : a_in) {
			out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return out;
	}

	bool IEquals(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size()) return false;
		for (std::size_t i = 0; i < a.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
				return false;
			}
		}
		return true;
	}

	// ISO-8601 UTC timestamp, "YYYY-MM-DDTHH:MM:SSZ".
	std::string Iso8601UtcNow()
	{
		const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm tm{};
#ifdef _WIN32
		gmtime_s(&tm, &now);
#else
		gmtime_r(&now, &tm);
#endif
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);
		return std::string(buf);
	}

	bool IsWindowsReservedName(std::string_view a_name)
	{
		static constexpr std::array<std::string_view, 23> kReserved = { {
			"CON", "PRN", "AUX", "NUL", "CLOCK$",
			"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
			"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
		} };
		for (const auto& r : kReserved) {
			if (IEquals(a_name, r)) return true;
		}
		return false;
	}

	void ScanDir(const std::filesystem::path& a_dir, bool a_builtin, std::vector<cs::features::imagespace::PresetMeta>& a_out)
	{
		std::error_code ec;
		if (!std::filesystem::exists(a_dir, ec) || !std::filesystem::is_directory(a_dir, ec)) {
			return;
		}
		for (auto it = std::filesystem::directory_iterator(a_dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			const auto& entry = *it;
			if (!entry.is_regular_file(ec)) continue;
			const auto& p = entry.path();
			if (p.extension() != ".toml") continue;
			const auto stem = p.stem().string();
			if (stem.empty() || stem.front() == '.') continue;  // hidden
			cs::features::imagespace::PresetMeta meta;
			meta.name     = stem;
			meta.identity = cs::features::imagespace::MakePresetIdentity(a_builtin ? 'B' : 'U', stem);
			meta.path     = p;
			meta.builtin  = a_builtin;
			a_out.push_back(std::move(meta));
		}
	}
}

namespace cs::features::imagespace
{
	std::string MakePresetIdentity(char a_scope, std::string_view a_name)
	{
		std::string id;
		id.reserve(2 + a_name.size());
		id.push_back(a_scope);
		id.push_back(':');
		id.append(ToLower(a_name));
		return id;
	}

	void PresetManager::Refresh()
	{
		_entries.clear();
		ScanDir(std::filesystem::path(kPresetsBuiltin), /*a_builtin=*/true,  _entries);
		ScanDir(std::filesystem::path(kPresetsRoot),    /*a_builtin=*/false, _entries);

		// User dir scan picked up Builtin/ as a subdir of Presets/; the file-only filter already
		// skipped subdirectories, but defensively de-dupe by identity should anyone drop the same
		// name twice (builtin wins; user copy is dropped with a warning).
		std::vector<PresetMeta> deduped;
		deduped.reserve(_entries.size());
		for (auto& e : _entries) {
			auto it = std::find_if(deduped.begin(), deduped.end(),
				[&](const PresetMeta& x) { return x.identity == e.identity; });
			if (it == deduped.end()) {
				deduped.push_back(std::move(e));
			} else {
				L->warn("preset name collision on '{}'; keeping {} copy",
					e.name, it->builtin ? "builtin" : "user");
			}
		}
		_entries = std::move(deduped);

		// Cross-scope name shadowing warning: a user file `Default.toml` and builtin `Default.toml`
		// land on different identities (`U:default` vs `B:default`), so both survive de-dupe.
		// FindByName(preferUser=true) and the marker payload resolver will silently pick the user
		// copy, which is usually fine but can confuse users tweaking a builtin. Log once per scan.
		for (size_t i = 0; i < _entries.size(); ++i) {
			for (size_t j = i + 1; j < _entries.size(); ++j) {
				if (_entries[i].builtin != _entries[j].builtin &&
					IEquals(_entries[i].name, _entries[j].name)) {
					L->warn("preset name '{}' exists as both builtin and user; bare-name lookups prefer the user copy",
						_entries[i].name);
				}
			}
		}

		std::sort(_entries.begin(), _entries.end(), [](const PresetMeta& a, const PresetMeta& b) {
			if (a.builtin != b.builtin) return a.builtin;  // builtin first.
			return ToLower(a.name) < ToLower(b.name);
		});
	}

	const PresetMeta* PresetManager::FindByIdentity(std::string_view a_identity) const
	{
		const std::string needle = ToLower(a_identity);
		for (const auto& e : _entries) {
			if (e.identity == needle) return &e;
		}
		return nullptr;
	}

	const PresetMeta* PresetManager::FindByName(std::string_view a_name, bool a_preferUser) const
	{
		const PresetMeta* builtinHit = nullptr;
		const PresetMeta* userHit    = nullptr;
		for (const auto& e : _entries) {
			if (IEquals(e.name, a_name)) {
				if (e.builtin) builtinHit = &e;
				else           userHit    = &e;
			}
		}
		if (a_preferUser && userHit)    return userHit;
		if (!a_preferUser && builtinHit) return builtinHit;
		return userHit ? userHit : builtinHit;
	}

	bool PresetManager::Load(const PresetMeta&     a_meta,
							 Imagespace::Settings& a_outSettings,
							 WeatherProfiles&      a_outProfiles,
							 std::string&          a_err) const
	{
		toml::table table;
		try {
			table = toml::parse_file(a_meta.path.string());
		} catch (const toml::parse_error& e) {
			std::ostringstream oss;
			oss << "TOML parse failed for " << a_meta.path.string() << ": " << e.description();
			a_err = oss.str();
			return false;
		}

		// Builtins must not stamp formID mappings into user state. Warn loudly if a shipped preset
		// somehow carries overrides so reviewers catch it.
		if (a_meta.builtin) {
			if (const auto* w = table["weather"].as_table(); w && (*w)["overrides"].as_table()) {
				L->warn("builtin preset '{}' contains [weather.overrides]; dropping (builtin presets must not ship formID mappings)",
					a_meta.name);
			}
		}

		Imagespace::Settings nextSettings{};
		WeatherProfiles      nextProfiles{};
		ParseSettings(table, nextSettings);
		ParseWeather(table, nextProfiles, /*a_dropOverrides=*/a_meta.builtin);

		a_outSettings = std::move(nextSettings);
		a_outProfiles = std::move(nextProfiles);
		return true;
	}

	bool PresetManager::Save(const std::filesystem::path& a_path,
							 const Imagespace::Settings&  a_settings,
							 const WeatherProfiles&       a_profiles,
							 std::string_view             a_presetName,
							 std::string&                 a_err,
							 bool                         a_allowOverwrite) const
	{
		if (!a_allowOverwrite && std::filesystem::exists(a_path)) {
			std::ostringstream oss;
			oss << "preset file already exists at " << a_path.string()
				<< " (pass a_allowOverwrite=true for the Save-active flow)";
			a_err = oss.str();
			return false;
		}

		toml::table table;
		EmitSettings(table, a_settings);
		EmitWeather(table, a_profiles, /*a_includeOverrides=*/true);

		auto& meta = table.insert_or_assign("meta", toml::table{}).first->second.as_table()->ref<toml::table>();
		meta.insert_or_assign("name",       std::string(a_presetName));
		meta.insert_or_assign("created_by", std::string("FO4CommunityShaders Imagespace"));
		meta.insert_or_assign("created_at", Iso8601UtcNow());

		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);

		std::ofstream out(a_path);
		if (!out) {
			std::ostringstream oss;
			oss << "failed to open " << a_path.string() << " for write";
			a_err = oss.str();
			return false;
		}
		out << table;
		if (!out) {
			std::ostringstream oss;
			oss << "write failed for " << a_path.string();
			a_err = oss.str();
			return false;
		}
		return true;
	}

	bool PresetManager::Delete(const PresetMeta& a_meta, std::string& a_err) const
	{
		if (a_meta.builtin) {
			a_err = "cannot delete builtin preset '" + a_meta.name + "'";
			return false;
		}
		std::error_code ec;
		if (!std::filesystem::remove(a_meta.path, ec) || ec) {
			std::ostringstream oss;
			oss << "delete failed for " << a_meta.path.string();
			if (ec) oss << ": " << ec.message();
			a_err = oss.str();
			return false;
		}
		return true;
	}

	bool ValidatePresetName(std::string_view               a_name,
							const std::vector<PresetMeta>& a_existing,
							std::string&                   a_err)
	{
		if (a_name.empty()) {
			a_err = "preset name is empty";
			return false;
		}
		if (a_name.size() > 64) {
			a_err = "preset name exceeds 64 characters";
			return false;
		}
		for (char c : a_name) {
			const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
			if (!ok) {
				a_err = "preset name contains invalid character; allowed: [A-Za-z0-9_-]";
				return false;
			}
		}
		if (IEquals(a_name, "Builtin")) {
			a_err = "'Builtin' is reserved";
			return false;
		}
		if (IsWindowsReservedName(a_name)) {
			a_err = "preset name is a reserved Windows device name";
			return false;
		}
		for (const auto& e : a_existing) {
			if (IEquals(e.name, a_name)) {
				a_err = "a preset named '" + std::string(a_name) + "' already exists";
				return false;
			}
		}
		return true;
	}

	bool ReadTextMarker(const std::filesystem::path& a_path, std::string& a_outPayload)
	{
		a_outPayload.clear();
		std::error_code ec;
		if (!std::filesystem::exists(a_path, ec)) return false;

		std::ifstream in(a_path, std::ios::binary);
		if (!in) return false;

		constexpr std::size_t kCap = 512;
		std::string buf(kCap, '\0');
		in.read(buf.data(), static_cast<std::streamsize>(kCap));
		const auto got = static_cast<std::size_t>(in.gcount());
		buf.resize(got);

		std::string_view view(buf);
		if (view.size() >= 3 && static_cast<unsigned char>(view[0]) == 0xEF &&
			static_cast<unsigned char>(view[1]) == 0xBB && static_cast<unsigned char>(view[2]) == 0xBF) {
			view.remove_prefix(3);
		}

		// First line.
		if (const auto eol = view.find_first_of("\r\n"); eol != std::string_view::npos) {
			view = view.substr(0, eol);
		}

		while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) view.remove_prefix(1);
		while (!view.empty() && (view.back()  == ' ' || view.back()  == '\t')) view.remove_suffix(1);

		if (view.empty()) return false;
		a_outPayload.assign(view);
		return true;
	}
}
