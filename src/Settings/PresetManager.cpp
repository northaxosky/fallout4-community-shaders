#include "Settings/PresetManager.h"

#include "Feature.h"
#include "Log.h"
#include "Menu/Menu.h"

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
	auto* L = cs::log::Get("cs.presets");

	constexpr std::string_view kPresetsRoot      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Presets";
	constexpr std::string_view kPresetsBuiltin   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Presets\\Builtin";
	constexpr std::string_view kGlobalConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\FO4CommunityShaders.toml";
	constexpr std::string_view kBootMarker       = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.cs_force_preset";

	class FeatureCallbackPassGuard
	{
	public:
		explicit FeatureCallbackPassGuard(cs::FeatureManager& a_manager) noexcept :
			_manager(a_manager)
		{}

		~FeatureCallbackPassGuard() { Finish(); }

		void Finish() noexcept
		{
			if (_finished)
				return;
			_manager.FinishRuntimeCallbackPass();
			_finished = true;
		}

	private:
		cs::FeatureManager& _manager;
		bool _finished = false;
	};

	struct PresetParticipant
	{
		cs::Feature* feature;
		std::string key;
	};

	bool CollectHealthyPresetParticipants(
		cs::FeatureManager& a_manager,
		std::string_view a_operation,
		std::string_view a_presetName,
		std::vector<PresetParticipant>& a_out,
		std::string& a_err)
	{
		a_out.clear();
		a_out.reserve(a_manager.GetAll().size());
		for (auto* feature : a_manager.GetAll()) {
			if (!feature
				|| !a_manager.PrepareRuntimeCallback(*feature, "PresetManager::ParticipatesInPresets")) {
				continue;
			}

			bool participates = false;
			try {
				participates = feature->ParticipatesInPresets();
			} catch (const std::exception& e) {
				a_manager.QuarantineRuntimeCallback(
					*feature,
					"PresetManager::ParticipatesInPresets",
					e.what());
				a_err = "preset participant metadata ParticipatesInPresets threw: " + std::string(e.what());
				return false;
			} catch (...) {
				a_manager.QuarantineRuntimeCallback(
					*feature,
					"PresetManager::ParticipatesInPresets",
					"non-standard exception");
				a_err = "preset participant metadata ParticipatesInPresets threw a non-standard exception";
				return false;
			}
			if (!participates)
				continue;

			if (!a_manager.PrepareRuntimeCallback(*feature, "PresetManager::GetPresetKey")) {
				a_err = "preset participant lost health before GetPresetKey";
				return false;
			}
			std::string key;
			try {
				key = feature->GetPresetKey();
			} catch (const std::exception& e) {
				a_manager.QuarantineRuntimeCallback(*feature, "PresetManager::GetPresetKey", e.what());
				a_err = "preset participant metadata GetPresetKey threw: " + std::string(e.what());
				return false;
			} catch (...) {
				a_manager.QuarantineRuntimeCallback(
					*feature,
					"PresetManager::GetPresetKey",
					"non-standard exception");
				a_err = "preset participant metadata GetPresetKey threw a non-standard exception";
				return false;
			}

			if (!a_manager.PrepareRuntimeCallback(*feature, "PresetManager::IsInTestMode")) {
				a_err = "preset participant '" + key + "' lost health before IsInTestMode";
				return false;
			}
			bool testMode = false;
			try {
				testMode = feature->IsInTestMode();
			} catch (const std::exception& e) {
				a_manager.QuarantineRuntimeCallback(*feature, "PresetManager::IsInTestMode", e.what());
				a_err = "preset participant '" + key + "' metadata IsInTestMode threw: " + e.what();
				return false;
			} catch (...) {
				a_manager.QuarantineRuntimeCallback(
					*feature,
					"PresetManager::IsInTestMode",
					"non-standard exception");
				a_err = "preset participant '" + key
					+ "' metadata IsInTestMode threw a non-standard exception";
				return false;
			}
			if (testMode) {
				L->info("{} preset '{}': skipping feature '{}' (test mode active)",
					a_operation, a_presetName, key);
				continue;
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

	void ScanDir(const std::filesystem::path& a_dir, bool a_builtin, std::vector<cs::PresetMeta>& a_out)
	{
		std::error_code ec;
		if (!std::filesystem::exists(a_dir, ec) || !std::filesystem::is_directory(a_dir, ec)) {
			return;
		}
		for (auto it = std::filesystem::directory_iterator(a_dir, ec);
			 !ec && it != std::filesystem::directory_iterator();
			 it.increment(ec))
		{
			const auto& entry = *it;
			if (!entry.is_regular_file(ec)) continue;
			const auto& p = entry.path();
			if (p.extension() != ".toml") continue;
			const auto stem = p.stem().string();
			if (stem.empty() || stem.front() == '.') continue;  // hidden
			cs::PresetMeta meta;
			meta.name     = stem;
			meta.identity = cs::MakePresetIdentity(a_builtin ? 'B' : 'U', stem);
			meta.path     = p;
			meta.builtin  = a_builtin;
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

	PresetManager& PresetManager::Get()
	{
		static PresetManager instance;
		return instance;
	}

	void PresetManager::Refresh()
	{
		_entries.clear();
		ScanDir(std::filesystem::path(kPresetsBuiltin), /*a_builtin=*/true,  _entries);
		ScanDir(std::filesystem::path(kPresetsRoot),    /*a_builtin=*/false, _entries);

		// Defensive de-dupe by identity (file shadowing edge cases).
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

		// Cross-scope same-name presets both survive; bare-name lookups prefer the user copy.
		for (std::size_t i = 0; i < _entries.size(); ++i) {
			for (std::size_t j = i + 1; j < _entries.size(); ++j) {
				if (_entries[i].builtin != _entries[j].builtin &&
					IEquals(_entries[i].name, _entries[j].name)) {
					L->warn("preset name '{}' exists as both builtin and user; bare-name lookups prefer the user copy",
						_entries[i].name);
				}
			}
		}

		std::sort(_entries.begin(), _entries.end(), [](const PresetMeta& a, const PresetMeta& b) {
			if (a.builtin != b.builtin) return a.builtin;
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

	bool PresetManager::Apply(const PresetMeta& a_meta, std::string& a_err)
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

		const auto* featuresTbl = table["features"].as_table();
		if (!featuresTbl) {
			a_err = "preset has no [features] table";
			return false;
		}

		// Unknown schema versions can still carry settings this build understands.
		if (const auto* metaTbl = table["meta"].as_table()) {
			if (const auto schemaNode = (*metaTbl)["schema_version"].value<std::int64_t>()) {
				if (*schemaNode != 1) {
					L->warn("Preset '{}' has [meta].schema_version={} but this build expects {}; loading known fields",
						a_meta.name, *schemaNode, 1);
				}
			}
		}

		PresetApplyContext ctx{ a_meta.builtin };
		auto& featureManager = FeatureManager::Get();
		FeatureCallbackPassGuard callbackPass(featureManager);

		std::vector<PresetParticipant> participants;
		if (!CollectHealthyPresetParticipants(
				featureManager,
				"Apply",
				a_meta.name,
				participants,
				a_err)) {
			return false;
		}

		// Phase 1: stage matching participants; skip test mode so smoke overrides survive.
		struct StageEntry
		{
			Feature*           feature;
			const toml::table* subtable;
			std::string        key;
		};
		std::vector<StageEntry> staged;
		staged.reserve(participants.size());

		for (const auto& [key, node] : *featuresTbl) {
			const auto* sub = node.as_table();
			if (!sub) continue;
			const std::string keyStr(key.str());
			const auto match = std::find_if(
				participants.begin(),
				participants.end(),
				[&keyStr](const PresetParticipant& a_participant) {
					return a_participant.key == keyStr;
				});
			if (match == participants.end()) {
				L->warn("preset '{}' references feature '{}' which is not healthy-active or participating; skipping",
					a_meta.name, keyStr);
			}
		}

		for (const auto& participant : participants) {
			const auto* node = featuresTbl->get(participant.key);
			const auto* sub = node ? node->as_table() : nullptr;
			if (sub)
				staged.push_back({ participant.feature, sub, participant.key });
		}
		if (staged.empty()) {
			a_err = "preset has no matching healthy active participants";
			return false;
		}

		for (const auto& entry : staged) {
			if (!featureManager.PrepareRuntimeCallback(
					*entry.feature,
					"PresetManager::StageFromPreset")) {
				a_err = "feature '" + entry.key + "' lost health before staging";
				return false;
			}

			std::string stageErr;
			bool stageAccepted = false;
			try {
				stageAccepted = entry.feature->StageFromPreset(*entry.subtable, ctx, stageErr);
			} catch (const std::exception& e) {
				featureManager.QuarantineRuntimeCallback(
					*entry.feature,
					"PresetManager::StageFromPreset",
					e.what());
				a_err = "feature '" + entry.key + "' staging threw: " + e.what();
				return false;
			} catch (...) {
				featureManager.QuarantineRuntimeCallback(
					*entry.feature,
					"PresetManager::StageFromPreset",
					"non-standard exception");
				a_err = "feature '" + entry.key + "' staging threw a non-standard exception";
				return false;
			}
			if (!stageAccepted) {
				std::ostringstream oss;
				oss << "feature '" << entry.key << "' failed to stage: "
					<< (stageErr.empty() ? "validation rejected preset state" : stageErr);
				a_err = oss.str();
				return false;
			}
		}

		for (const auto& entry : staged) {
			if (!featureManager.PrepareRuntimeCallback(
					*entry.feature,
					"PresetManager::CommitStagedSwap")) {
				a_err = "feature '" + entry.key + "' lost health before preset swap; no live state changed";
				return false;
			}
		}

		// Phase 2a: no-throw/no-I/O swap all staged scratch into live state before any finalize can fail.
		for (const auto& entry : staged) {
			entry.feature->CommitStagedSwap();
		}

		// Phase 2b: persist and rebuild derived resources; failures log but do NOT abort other features.
		// Live state stays consistent even when a failing feature's on-disk snapshot is stale.
		std::vector<std::string> finalizeErrors;
		for (const auto& entry : staged) {
			if (!featureManager.PrepareRuntimeCallback(
					*entry.feature,
					"PresetManager::CommitStagedFinalize")) {
				finalizeErrors.emplace_back(
					"feature '" + entry.key + "' finalize skipped because its owner is no longer healthy");
				L->error("{}", finalizeErrors.back());
				continue;
			}

			try {
				entry.feature->CommitStagedFinalize();
			} catch (const std::exception& ex) {
				featureManager.QuarantineRuntimeCallback(
					*entry.feature,
					"PresetManager::CommitStagedFinalize",
					ex.what());
				std::ostringstream oss;
				oss << "feature '" << entry.key << "' finalize failed: " << ex.what();
				finalizeErrors.emplace_back(oss.str());
				L->error("{}", finalizeErrors.back());
			} catch (...) {
				featureManager.QuarantineRuntimeCallback(
					*entry.feature,
					"PresetManager::CommitStagedFinalize",
					"non-standard exception");
				std::ostringstream oss;
				oss << "feature '" << entry.key << "' finalize failed: unknown exception";
				finalizeErrors.emplace_back(oss.str());
				L->error("{}", finalizeErrors.back());
			}
		}
		callbackPass.Finish();

		if (!finalizeErrors.empty()) {
			std::ostringstream oss;
			oss << "live preset state was swapped, but " << finalizeErrors.size()
				<< " finalize error(s) left feature files stale; active preset identity was not persisted; see log";
			a_err = oss.str();
			L->warn("Applied preset: {} ({}, {} feature(s)) with {} finalize error(s); "
			        "active preset on disk left unchanged so next boot reapplies the previous state",
				a_meta.name, a_meta.builtin ? "builtin" : "user", staged.size(), finalizeErrors.size());
			// Disk is stale for at least one feature; skip SaveCoreConfig so relaunch restores known-good.
			Menu::ShowToast("Applied '" + a_meta.name + "' with " +
				std::to_string(finalizeErrors.size()) + " save error(s); active preset NOT persisted", 5.0);
			return false;
		}

		activeIdentity       = a_meta.identity;
		activeName           = a_meta.name;
		pendingComboIdentity = a_meta.identity;
		SaveCoreConfig();

		L->info("Applied preset: {} ({}, {} feature(s))", a_meta.name,
			a_meta.builtin ? "builtin" : "user", staged.size());
		Menu::ShowToast("Applied preset '" + a_meta.name + "'", 2.5);
		return true;
	}

	bool PresetManager::Save(const std::filesystem::path& a_path,
							 std::string_view             a_presetName,
							 std::string&                 a_err,
							 bool                         a_allowOverwrite)
	{
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
		FeatureCallbackPassGuard callbackPass(featureManager);
		std::vector<PresetParticipant> participants;
		if (!CollectHealthyPresetParticipants(
				featureManager,
				"Save",
				a_presetName,
				participants,
				a_err)) {
			return false;
		}

		std::size_t emitted = 0;
		for (const auto& participant : participants) {
			if (!featureManager.PrepareRuntimeCallback(
					*participant.feature,
					"PresetManager::ExportToPreset")) {
				a_err = "feature '" + participant.key + "' lost health before preset export";
				return false;
			}

			toml::table sub;
			try {
				participant.feature->ExportToPreset(sub);
			} catch (const std::exception& e) {
				featureManager.QuarantineRuntimeCallback(
					*participant.feature,
					"PresetManager::ExportToPreset",
					e.what());
				a_err = "feature '" + participant.key + "' preset export threw: " + e.what();
				return false;
			} catch (...) {
				featureManager.QuarantineRuntimeCallback(
					*participant.feature,
					"PresetManager::ExportToPreset",
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
		meta.insert_or_assign("name",           std::string(a_presetName));
		meta.insert_or_assign("schema_version", static_cast<std::int64_t>(1));
		meta.insert_or_assign("created_by",     std::string("FO4CommunityShaders"));
		meta.insert_or_assign("created_at",     Iso8601UtcNow());
		callbackPass.Finish();

		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);

		// TOCTOU re-check: refuse if the file appeared after caller validation.
		if (!a_allowOverwrite && std::filesystem::exists(a_path)) {
			std::ostringstream oss;
			oss << "preset file appeared between validation and write at " << a_path.string();
			a_err = oss.str();
			return false;
		}

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

	void PresetManager::LoadCoreConfig()
	{
		activeIdentity.clear();
		activeName.clear();
		autoLoadOnBoot = false;

		toml::table table;
		try {
			table = toml::parse_file(kGlobalConfigPath);
		} catch (const toml::parse_error&) {
			return;
		}

		const auto* presetTbl = table["preset"].as_table();
		if (!presetTbl) return;

		if (const auto v = (*presetTbl)["active"].value<std::string>()) {
			activeIdentity = ToLower(*v);
		}
		autoLoadOnBoot = (*presetTbl)["auto_load_on_boot"].value_or(false);
	}

	bool PresetManager::SaveCoreConfig()
	{
		// Parse-merge-write preserves Feature.cpp-owned siblings; parse failure leaves the file untouched.
		toml::table table;
		const bool fileExists = std::filesystem::exists(kGlobalConfigPath);
		if (fileExists) {
			try {
				table = toml::parse_file(kGlobalConfigPath);
			} catch (const toml::parse_error& e) {
				L->warn("SaveCoreConfig: skipping write; failed to parse {} ({}). The [preset] block is not persisted this call.",
					kGlobalConfigPath, e.description());
				return false;
			}
		}

		if (!table["preset"].as_table()) {
			table.insert_or_assign("preset", toml::table{});
		}
		auto& p = *table["preset"].as_table();
		p.insert_or_assign("active",            activeIdentity);
		p.insert_or_assign("auto_load_on_boot", autoLoadOnBoot);

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(kGlobalConfigPath).parent_path(), ec);

		std::filesystem::path outPath{ kGlobalConfigPath };
		std::ofstream         out(outPath);
		if (!out) return false;
		out << table;
		return out.good();
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

		// Marker wins over auto-load; invalid marker clears activeIdentity for deterministic smoke runs.
		std::string markerPayload;
		if (ReadTextMarker(std::filesystem::path(kBootMarker), markerPayload)) {
			const PresetMeta* meta = nullptr;
			if (markerPayload.size() >= 2 && markerPayload[1] == ':' &&
				(markerPayload[0] == 'B' || markerPayload[0] == 'U' ||
				 markerPayload[0] == 'b' || markerPayload[0] == 'u'))
			{
				meta = FindByIdentity(markerPayload);
			} else {
				meta = FindByName(markerPayload, /*a_preferUser=*/true);
			}
			if (meta) {
				std::string err;
				if (Apply(*meta, err)) {
					L->info("boot preset marker honoured: '{}' -> {}", markerPayload, meta->identity);
				} else {
					lastError = err;
					L->warn("boot preset marker '{}' apply failed: {}", markerPayload, err);
				}
			} else {
				L->warn("boot preset marker payload '{}' did not resolve; clearing active identity", markerPayload);
				activeIdentity.clear();
				activeName.clear();
				SaveCoreConfig();
			}
			pendingComboIdentity = activeIdentity;
			return;
		}

		if (autoLoadOnBoot && !activeIdentity.empty()) {
			const PresetMeta* meta = FindByIdentity(activeIdentity);
			if (!meta && !activeName.empty()) {
				meta = FindByName(activeName, /*a_preferUser=*/true);
			}
			if (meta) {
				std::string err;
				if (!Apply(*meta, err)) {
					lastError = err;
					L->warn("auto-load preset '{}' apply failed: {}", meta->name, err);
				}
			} else {
				L->warn("auto-load preset '{}' not found; per-feature TOML settings remain in effect",
					activeName.empty() ? activeIdentity : activeName);
			}
		}

		pendingComboIdentity = activeIdentity;
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
			const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
							(c >= '0' && c <= '9') || c == '_' || c == '-';
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
		if (view.size() >= 3 &&
			static_cast<unsigned char>(view[0]) == 0xEF &&
			static_cast<unsigned char>(view[1]) == 0xBB &&
			static_cast<unsigned char>(view[2]) == 0xBF)
		{
			view.remove_prefix(3);
		}
		if (const auto eol = view.find_first_of("\r\n"); eol != std::string_view::npos) {
			view = view.substr(0, eol);
		}
		while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) view.remove_prefix(1);
		while (!view.empty() && (view.back()  == ' ' || view.back()  == '\t')) view.remove_suffix(1);

		if (view.empty()) return false;
		a_outPayload.assign(view);
		in.close();
		std::error_code rmEc;
		std::filesystem::remove(a_path, rmEc);
		return true;
	}
}
