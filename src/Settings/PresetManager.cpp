#include "Settings/PresetManager.h"

#include "Feature.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Settings/FeatureConfig.h"
#include "Settings/SettingsRegistry.h"
#include "Utils/CSUtil.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <toml++/toml.hpp>

namespace
{
	auto* L = cs::log::Get("cs.presets");

	constexpr std::string_view kPresetsRoot = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Presets";
	constexpr std::string_view kPresetsBuiltin = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Presets\\Builtin";

	struct PresetParticipant
	{
		cs::Feature* feature;
		std::string key;
	};

	bool PresetKeyMatches(std::string_view a_key, std::string_view a_configKey)
	{
		const auto normalize = [](std::string_view key) {
			return key | std::views::filter([](char c) { return c != '_'; }) |
			       std::views::transform([](unsigned char c) { return std::tolower(c); });
		};
		// Earlier presets used lowercase and snake_case feature names.
		return std::ranges::equal(normalize(a_key), normalize(a_configKey));
	}

	bool CollectHealthyPresetParticipants(
		cs::FeatureManager& a_manager,
		std::vector<PresetParticipant>& a_out,
		std::string& a_err)
	{
		a_out.clear();
		for (auto* feature : a_manager.GetAll()) {
			if (!a_manager.PrepareRuntimeCallback(*feature, "PresetManager::Collect")) {
				continue;
			}
			const auto& access = feature->GetLiveSettingsAccess();
			if (!access.snapshot || !access.prepare)
				continue;
			std::string key;
			try {
				key = feature->GetConfigKey();
			} catch (const std::exception& e) {
				a_manager.QuarantineRuntimeCallback(*feature, "PresetManager::Collect", e.what());
				a_err = "preset participant metadata threw: " + std::string(e.what());
				return false;
			} catch (...) {
				a_manager.QuarantineRuntimeCallback(*feature, "PresetManager::Collect", "non-standard exception");
				a_err = "preset participant metadata threw a non-standard exception";
				return false;
			}
			a_out.push_back({ feature, std::move(key) });
		}
		return true;
	}

	std::string ToLower(std::string_view a_in)
	{
		std::string out;
		out.reserve(a_in.size());
		for (char c : a_in) {
			out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return out;
	}

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
			"CON",
			"PRN",
			"AUX",
			"NUL",
			"CLOCK$",
			"COM1",
			"COM2",
			"COM3",
			"COM4",
			"COM5",
			"COM6",
			"COM7",
			"COM8",
			"COM9",
			"LPT1",
			"LPT2",
			"LPT3",
			"LPT4",
			"LPT5",
			"LPT6",
			"LPT7",
			"LPT8",
			"LPT9",
		} };
		for (const auto& r : kReserved) {
			if (cs::util::EqualsIgnoreCase(a_name, r))
				return true;
		}
		return false;
	}

	void ScanDir(const std::filesystem::path& a_dir, bool a_builtin, std::vector<cs::PresetMeta>& a_out)
	{
		std::error_code ec;
		if (!std::filesystem::exists(a_dir, ec) || !std::filesystem::is_directory(a_dir, ec)) {
			return;
		}
		for (auto it = std::filesystem::directory_iterator(a_dir, ec);
			!ec && it != std::filesystem::directory_iterator();
			it.increment(ec)) {
			const auto& entry = *it;
			if (!entry.is_regular_file(ec))
				continue;
			const auto& p = entry.path();
			if (p.extension() != ".toml")
				continue;
			const auto stem = p.stem().string();
			if (stem.empty() || stem.front() == '.')
				continue;  // hidden
			cs::PresetMeta meta;
			meta.name = stem;
			meta.identity = cs::MakePresetIdentity(a_builtin ? 'B' : 'U', stem);
			meta.path = p;
			meta.builtin = a_builtin;
			a_out.push_back(std::move(meta));
		}
	}
}

namespace cs
{
	std::string MakePresetIdentity(char a_scope, std::string_view a_name)
	{
		std::string id;
		id.reserve(2 + a_name.size());
		id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(a_scope))));
		id.push_back(':');
		id.append(ToLower(a_name));
		return id;
	}

	PresetManager::PresetManager() :
		activeIdentity(settings::core::Preset{}.active),
		autoLoadOnBoot(settings::core::Preset{}.autoLoadOnBoot)
	{}

	PresetManager& PresetManager::Get()
	{
		static PresetManager instance;
		return instance;
	}

	void PresetManager::Refresh()
	{
		_entries.clear();
		ScanDir(std::filesystem::path(kPresetsBuiltin), true, _entries);
		ScanDir(std::filesystem::path(kPresetsRoot), false, _entries);

		// Deduplicate file-shadowing edge cases.
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

		// Bare names prefer user presets.
		for (std::size_t i = 0; i < _entries.size(); ++i) {
			for (std::size_t j = i + 1; j < _entries.size(); ++j) {
				if (_entries[i].builtin != _entries[j].builtin &&
					cs::util::EqualsIgnoreCase(_entries[i].name, _entries[j].name)) {
					L->warn("preset name '{}' exists as both builtin and user; bare-name lookups prefer the user copy",
						_entries[i].name);
				}
			}
		}

		std::sort(_entries.begin(), _entries.end(), [](const PresetMeta& a, const PresetMeta& b) {
			if (a.builtin != b.builtin)
				return a.builtin;
			return ToLower(a.name) < ToLower(b.name);
		});
	}

	const PresetMeta* PresetManager::FindByIdentity(std::string_view a_identity) const
	{
		const std::string needle = ToLower(a_identity);
		for (const auto& e : _entries) {
			if (e.identity == needle)
				return &e;
		}
		return nullptr;
	}

	const PresetMeta* PresetManager::FindByName(std::string_view a_name) const
	{
		const PresetMeta* builtinHit = nullptr;
		const PresetMeta* userHit = nullptr;
		for (const auto& e : _entries) {
			if (cs::util::EqualsIgnoreCase(e.name, a_name)) {
				if (e.builtin)
					builtinHit = &e;
				else
					userHit = &e;
			}
		}
		return userHit ? userHit : builtinHit;
	}

	bool PresetManager::Apply(const PresetMeta& a_meta, std::string& a_err)
	{
		if (settings::liveComparisonActive) {
			a_err = "Stop the A/B comparison before applying a preset";
			return false;
		}
		toml::table table;
		try {
			table = toml::parse_file(a_meta.path.string());
		} catch (const toml::parse_error& e) {
			std::ostringstream oss;
			oss << "TOML parse failed for " << a_meta.path.string() << ": " << e.description();
			a_err = oss.str();
			return false;
		}

		const auto* featuresTbl = table["features"].as_table();
		if (!featuresTbl) {
			a_err = "preset has no [features] table";
			return false;
		}

		if (table["meta"]["schema_version"].value<std::int64_t>() != 1) {
			a_err = "preset [meta].schema_version must be 1";
			return false;
		}

		auto& featureManager = FeatureManager::Get();

		std::vector<PresetParticipant> participants;
		if (!CollectHealthyPresetParticipants(
				featureManager,
				participants,
				a_err)) {
			return false;
		}

		struct StageEntry
		{
			Feature* feature;
			const toml::table* subtable;
			std::string key;
		};
		std::vector<StageEntry> staged;
		staged.reserve(participants.size());

		for (const auto& [key, node] : *featuresTbl) {
			const auto* sub = node.as_table();
			if (!sub)
				continue;
			const auto match = std::ranges::find_if(
				participants,
				[&](const PresetParticipant& a_participant) {
					return PresetKeyMatches(key.str(), a_participant.key);
				});
			if (match == participants.end()) {
				L->warn("preset '{}' references feature '{}' which is not healthy-active or participating; skipping",
					a_meta.name, key.str());
				continue;
			}
			if (std::ranges::find(staged, match->feature, &StageEntry::feature) == staged.end())
				staged.push_back({ match->feature, sub, match->key });
		}
		if (staged.empty()) {
			a_err = "preset has no matching healthy active participants";
			return false;
		}

		std::vector<settings::PreparedLiveSettings> prepared;
		for (const auto& entry : staged) {
			try {
				auto value = featureManager.PrepareLiveSettingsFromOverrides(
					*entry.feature, entry.subtable, nullptr, "PresetManager::Finalize", a_err);
				if (!value) {
					a_err = "feature '" + entry.key + "' failed to stage: " + a_err;
					return false;
				}
				prepared.push_back(std::move(*value));
			} catch (const std::exception& e) {
				featureManager.QuarantineRuntimeCallback(
					*entry.feature,
					"PresetManager::Prepare",
					e.what());
				a_err = "feature '" + entry.key + "' staging threw: " + e.what();
				return false;
			} catch (...) {
				featureManager.QuarantineRuntimeCallback(
					*entry.feature,
					"PresetManager::Prepare",
					"non-standard exception");
				a_err = "feature '" + entry.key + "' staging threw a non-standard exception";
				return false;
			}
		}
		try {
			settings::ApplyPreparedLiveSettings(prepared);
		} catch (const std::exception& e) {
			a_err = std::string("preset finalization failed: ") + e.what();
			return false;
		} catch (...) {
			a_err = "preset finalization failed: non-standard exception";
			return false;
		}

		// Finalization failures leave disk stale but memory consistent.
		std::vector<std::string> finalizeErrors;
		for (const auto& entry : staged) {
			if (!featureManager.PrepareRuntimeCallback(
					*entry.feature,
					"PresetManager::Save")) {
				finalizeErrors.emplace_back(
					"feature '" + entry.key + "' finalize skipped because its owner is no longer healthy");
				L->error("{}", finalizeErrors.back());
				continue;
			}

			try {
				if (!entry.feature->Persist()) {
					finalizeErrors.emplace_back(
						"feature '" + entry.key + "' finalize failed to save settings");
					L->error("{}", finalizeErrors.back());
				}
			} catch (const std::exception& ex) {
				featureManager.QuarantineRuntimeCallback(
					*entry.feature,
					"PresetManager::Save",
					ex.what());
				std::ostringstream oss;
				oss << "feature '" << entry.key << "' finalize failed: " << ex.what();
				finalizeErrors.emplace_back(oss.str());
				L->error("{}", finalizeErrors.back());
			} catch (...) {
				featureManager.QuarantineRuntimeCallback(
					*entry.feature,
					"PresetManager::Save",
					"non-standard exception");
				std::ostringstream oss;
				oss << "feature '" << entry.key << "' finalize failed: unknown exception";
				finalizeErrors.emplace_back(oss.str());
				L->error("{}", finalizeErrors.back());
			}
		}

		if (!finalizeErrors.empty()) {
			std::ostringstream oss;
			oss << "live preset state was swapped, but " << finalizeErrors.size()
				<< " finalize error(s) left feature files stale; active preset identity was not persisted; see log";
			a_err = oss.str();
			L->warn(
				"Applied preset: {} ({}, {} feature(s)) with {} finalize error(s); "
				"active preset on disk left unchanged so next boot reapplies the previous state",
				a_meta.name, a_meta.builtin ? "builtin" : "user", staged.size(), finalizeErrors.size());
			// Skip saving when any feature leaves disk stale.
			Menu::ShowToast("Applied '" + a_meta.name + "' with " +
								std::to_string(finalizeErrors.size()) + " save error(s); active preset NOT persisted",
				5.0,
				DMUI_STATUS_SEVERITY_ERROR,
				a_meta.name);
			return false;
		}

		activeIdentity = a_meta.identity;
		activeName = a_meta.name;
		pendingComboIdentity = a_meta.identity;
		if (!SaveCoreConfig()) {
			a_err = "live preset state was swapped, but active preset identity could not be saved";
			L->error("{}", a_err);
			Menu::ShowToast("Applied '" + a_meta.name + "' but active preset could not be saved",
				5.0, DMUI_STATUS_SEVERITY_ERROR, a_meta.name);
			return false;
		}

		L->info("Applied preset: {} ({}, {} feature(s))", a_meta.name,
			a_meta.builtin ? "builtin" : "user", staged.size());
		Menu::ShowToast("Applied preset '" + a_meta.name + "'", 2.5, DMUI_STATUS_SEVERITY_INFO, a_meta.name);
		return true;
	}

	bool PresetManager::Save(const std::filesystem::path& a_path,
		std::string_view a_presetName,
		std::string& a_err,
		bool a_allowOverwrite)
	{
		if (settings::liveComparisonActive) {
			a_err = "Stop the A/B comparison before saving a preset";
			return false;
		}
		if (!a_allowOverwrite && std::filesystem::exists(a_path)) {
			std::ostringstream oss;
			oss << "preset file already exists at " << a_path.string();
			a_err = oss.str();
			return false;
		}

		toml::table table;
		table.insert_or_assign("features", toml::table{});
		auto& featuresTbl = *table["features"].as_table();

		auto& featureManager = FeatureManager::Get();
		std::vector<PresetParticipant> participants;
		if (!CollectHealthyPresetParticipants(
				featureManager,
				participants,
				a_err)) {
			return false;
		}

		std::size_t emitted = 0;
		for (const auto& participant : participants) {
			if (!featureManager.PrepareRuntimeCallback(
					*participant.feature,
					"PresetManager::Snapshot")) {
				a_err = "feature '" + participant.key + "' lost health before preset export";
				return false;
			}

			toml::table sub;
			try {
				sub = participant.feature->GetLiveSettingsAccess().snapshot();
			} catch (const std::exception& e) {
				featureManager.QuarantineRuntimeCallback(
					*participant.feature,
					"PresetManager::Snapshot",
					e.what());
				a_err = "feature '" + participant.key + "' preset export threw: " + e.what();
				return false;
			} catch (...) {
				featureManager.QuarantineRuntimeCallback(
					*participant.feature,
					"PresetManager::Snapshot",
					"non-standard exception");
				a_err = "feature '" + participant.key + "' preset export threw a non-standard exception";
				return false;
			}
			if (sub.empty()) {
				continue;
			}
			featuresTbl.insert_or_assign(participant.key, std::move(sub));
			++emitted;
		}

		if (emitted == 0) {
			a_err = "no participating features produced preset state";
			return false;
		}

		table.insert_or_assign("meta", toml::table{});
		auto& meta = *table["meta"].as_table();
		meta.insert_or_assign("name", std::string(a_presetName));
		meta.insert_or_assign("schema_version", static_cast<std::int64_t>(1));
		meta.insert_or_assign("created_by", std::string("FO4CommunityShaders"));
		meta.insert_or_assign("created_at", Iso8601UtcNow());

		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);
		if (ec) {
			a_err = "failed to create preset directory: " + ec.message();
			return false;
		}

		std::ofstream out(a_path, std::ios::binary |
									  (a_allowOverwrite ? std::ios::trunc : std::ios::noreplace));
		if (!out) {
			std::ostringstream oss;
			oss << "failed to open " << a_path.string() << " for write";
			a_err = oss.str();
			return false;
		}
		out << table;
		out.close();
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
			if (ec)
				oss << ": " << ec.message();
			a_err = oss.str();
			return false;
		}
		return true;
	}

	void PresetManager::LoadCoreConfig()
	{
		const settings::core::Preset defaults;
		activeIdentity = defaults.active;
		activeName.clear();
		autoLoadOnBoot = defaults.autoLoadOnBoot;

		const auto table = cs::feature_config::GetRoot();
		const auto* presetTbl = table["preset"].as_table();
		if (!presetTbl)
			return;

		settings::core::Preset preset;
		if (std::string error; !settings::ParseTable(settings::core::kPreset, *presetTbl, preset, error)) {
			L->warn("preset.{}; using preset defaults", error);
			return;
		}
		activeIdentity = ToLower(preset.active);
		autoLoadOnBoot = preset.autoLoadOnBoot;
	}

	bool PresetManager::SaveCoreConfig()
	{
		const auto p = settings::SerializeFull(settings::core::kPreset, settings::core::Preset{
																			.active = activeIdentity,
																			.autoLoadOnBoot = autoLoadOnBoot });

		const auto result = cs::feature_config::UpdateTopLevelSection("preset", p);
		if (!result) {
			L->warn("SaveCoreConfig failed: {}", result.error);
		}
		return result.success;
	}

	void PresetManager::ResolveAndApplyBootPreset()
	{
		LoadCoreConfig();
		Refresh();

		if (!activeIdentity.empty()) {
			if (const auto* meta = FindByIdentity(activeIdentity)) {
				activeName = meta->name;
			}
		}

		if (autoLoadOnBoot && !activeIdentity.empty()) {
			const PresetMeta* meta = FindByIdentity(activeIdentity);
			if (meta) {
				std::string err;
				if (!Apply(*meta, err)) {
					lastError = err;
					L->warn("auto-load preset '{}' apply failed: {}", meta->name, err);
				}
			} else {
				L->warn("auto-load preset '{}' not found; unified TOML settings remain in effect",
					activeIdentity);
			}
		}

		pendingComboIdentity = activeIdentity;
	}

	bool ValidatePresetName(std::string_view a_name,
		const std::vector<PresetMeta>& a_existing,
		std::string& a_err)
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
			const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			                (c >= '0' && c <= '9') || c == '_' || c == '-';
			if (!ok) {
				a_err = "preset name contains invalid character; allowed: [A-Za-z0-9_-]";
				return false;
			}
		}
		if (cs::util::EqualsIgnoreCase(a_name, "Builtin")) {
			a_err = "'Builtin' is reserved";
			return false;
		}
		if (IsWindowsReservedName(a_name)) {
			a_err = "preset name is a reserved Windows device name";
			return false;
		}
		for (const auto& e : a_existing) {
			if (cs::util::EqualsIgnoreCase(e.name, a_name)) {
				a_err = "a preset named '" + std::string(a_name) + "' already exists";
				return false;
			}
		}
		return true;
	}
}
