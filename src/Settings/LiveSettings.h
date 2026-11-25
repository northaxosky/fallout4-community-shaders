#pragma once

#include "Render/FrameThread.h"
#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

namespace cs::features::performance_overlay
{
	class ABComparison;
}

namespace cs::settings
{
	// kDeferred remembers the keys for the next kNow commit or save.
	enum class Persist : std::uint8_t
	{
		kNo,
		kDeferred,
		kNow
	};

	// kAll also admits restart-only keys (next launch).
	enum class Scope : std::uint8_t
	{
		kImmediate,
		kAll
	};

	inline std::atomic_bool liveComparisonActive{ false };
	inline constexpr std::string_view kComparisonRefusal = "an A/B comparison is active; stop it in the Performance Overlay first";

	// Only an A/B comparison may commit to shared settings while one runs.
	class ComparisonKey
	{
		friend class features::performance_overlay::ABComparison;
		constexpr ComparisonKey() noexcept = default;
	};

	struct PreparedLiveSettings
	{
		std::function<void()> swap;
		std::function<void()> finalize;
		// Runs after finalize succeeds; false when the write failed.
		std::function<bool()> persist;
	};

	// The UI edits accumulated for one feature, posted as a single commit.
	struct PendingCommit
	{
		toml::table values;
		Persist persist = Persist::kDeferred;
	};

	// The one route into a feature's settings; see LiveSettings.
	class LiveSettingsAccess
	{
	public:
		virtual ~LiveSettingsAccess() = default;

		virtual SchemaView Schema() const = 0;
		// Live immediate fields.
		virtual toml::table Snapshot() const = 0;
		// Every field.
		virtual toml::table Defaults() const = 0;
		// Frame boundary only: builds the candidate and its swap.
		virtual std::optional<PreparedLiveSettings> Prepare(const toml::table& a_values, Scope a_scope, Persist a_persist, std::string& a_error) = 0;
		// UI: resyncs the draft when a commit landed since the last call.
		virtual void BeginDraft() = 0;
		// UI: queues draft changes; true when the caller posts.
		virtual bool QueueEdits(bool a_pageExit) = 0;
		// Frame boundary: the pending commit, if any.
		virtual std::optional<PendingCommit> TakePending() = 0;
		// Makes the next BeginDraft resync after a failed commit.
		virtual void Resync() noexcept = 0;
		// Writes the keys committed with kDeferred.
		virtual bool Flush() = 0;
		// Writes every immediate field and the deferred keys.
		virtual bool Save() = 0;
		virtual bool Modified() const = 0;
		// Restart-only field descriptions that differ from live.
		virtual std::vector<std::string_view> RestartPending() const = 0;
		// Reads next-launch values from `[features.X]`; no write.
		virtual void ReloadNext(const toml::table* a_stored) = 0;

		// Registry section to persist to; stamped by FeatureManager.
		std::vector<std::string> section;
		// Live-settable keys that an A/B comparison must not capture.
		std::vector<std::string_view> notComparable;
		// False skips presets, A/B, Remote Control and the A/B lock.
		bool shared = true;
		// UI thread: an edit finished since the last QueueEdits.
		bool editCompleted = false;
		// The owning feature's channel; stamped by FeatureManager.
		spdlog::logger* log = nullptr;

	protected:
		spdlog::logger& Logger() const noexcept { return log ? *log : *spdlog::default_logger_raw(); }
	};

	// A feature's live settings; commits swap the whole struct.
	template <const auto& kSchema>
	class LiveSettings final : public LiveSettingsAccess
	{
	public:
		using Settings = typename std::remove_cvref_t<decltype(kSchema)>::SettingsType;

		struct Hooks
		{
			// Clamps a candidate; runs on every path.
			std::function<void(Settings&)> normalize;
			// After the swap; a throw rolls back.
			std::function<void(const Settings& a_previous)> finalize;
			// Defaults that depend on runtime state.
			std::function<Settings()> defaults;
			std::vector<std::string_view> notComparable;
			// Held across the swap for readers that lock it.
			std::mutex* swapGuard = nullptr;
			bool shared = true;
		};

		LiveSettings() :
			LiveSettings(Hooks{}) {}

		explicit LiveSettings(Hooks a_hooks) :
			_hooks(std::move(a_hooks))
		{
			notComparable = _hooks.notComparable;
			shared = _hooks.shared;
		}

		// Boot seed: live, next-launch and draft start stored.
		bool Configure(const toml::table& a_featureTable, std::string& a_error)
		{
			Settings candidate{};
			if (!Parse(kSchema, a_featureTable, candidate, a_error))
				return false;
			if (_hooks.normalize)
				_hooks.normalize(candidate);
			std::scoped_lock lock(_mutex);
			_current = _next = _draft = std::move(candidate);
			_draftVersion = _version.load(std::memory_order_acquire);
			return true;
		}

		const Settings& Get() const noexcept { return _current; }
		// UI thread only; widgets edit this, never Get().
		Settings& Draft() noexcept { return _draft; }
		Settings DefaultSettings() const { return _hooks.defaults ? _hooks.defaults() : Settings{}; }

		SchemaView Schema() const override { return MakeSchemaView(kSchema); }

		toml::table Snapshot() const override
		{
			toml::table table;
			std::scoped_lock lock(_mutex);
			std::apply([&](const auto&... a_fields) {
				([&] {
					if (a_fields.timing == ApplyTiming::kImmediate)
						a_fields.Write(table, _current);
				}(),
					...);
			},
				kSchema.fields);
			return table;
		}

		toml::table Defaults() const override { return SerializeFull(kSchema, DefaultSettings()); }

		std::optional<PreparedLiveSettings> Prepare(const toml::table& a_values, Scope a_scope, Persist a_persist, std::string& a_error) override
		{
			render::CheckFrameBoundary("settings::LiveSettings::Prepare");
			Settings candidate, next;
			{
				std::scoped_lock lock(_mutex);
				candidate = _current;
				next = _next;
			}
			std::set<std::string, std::less<>> named;
			bool valid = true;
			std::apply([&](const auto&... a_fields) {
				([&] {
					if (!valid || !a_values.contains(a_fields.key))
						return;
					const bool immediate = a_fields.timing == ApplyTiming::kImmediate;
					if (!immediate && a_scope != Scope::kAll)
						return;
					valid = a_fields.Read(a_values, immediate ? candidate : next, a_error);
					named.emplace(a_fields.key);
				}(),
					...);
			},
				kSchema.fields);
			if (!valid)
				return std::nullopt;
			if (_hooks.normalize) {
				const auto before = candidate;
				_hooks.normalize(candidate);
				for (const auto& [key, value] : SerializeDelta(kSchema, candidate, before))
					named.emplace(key.str());
			}

			// After the swap the holders hold the previous values.
			const auto held = std::make_shared<Settings>(std::move(candidate));
			const auto heldNext = std::make_shared<Settings>(std::move(next));
			PreparedLiveSettings prepared;
			prepared.swap = [this, held, heldNext] {
				std::unique_lock<std::mutex> guard;
				if (_hooks.swapGuard)
					guard = std::unique_lock<std::mutex>(*_hooks.swapGuard);
				std::scoped_lock lock(_mutex);
				using std::swap;
				swap(_current, *held);
				swap(_next, *heldNext);
				_version.fetch_add(1, std::memory_order_acq_rel);
			};
			if (_hooks.finalize)
				prepared.finalize = [this, held] { _hooks.finalize(*held); };
			if (a_persist != Persist::kNo) {
				prepared.persist = [this, named = std::move(named), a_persist] {
					{
						std::scoped_lock lock(_mutex);
						_unsaved.insert(named.begin(), named.end());
					}
					return a_persist == Persist::kNow ? Flush() : true;
				};
			}
			return prepared;
		}

		void BeginDraft() override
		{
			std::scoped_lock lock(_mutex);
			const auto version = _version.load(std::memory_order_acquire);
			if (version == _draftVersion)
				return;
			_draft = Composed();
			_draftVersion = version;
		}

		bool QueueEdits(bool a_pageExit) override
		{
			const bool completed = std::exchange(editCompleted, false);
			const bool flush = a_pageExit || completed;
			std::scoped_lock lock(_mutex);
			auto edits = SerializeDelta(kSchema, _draft, Composed());
			// Text commits when the edit completes, not per keystroke.
			if (!flush) {
				std::apply([&](const auto&... a_fields) {
					([&] {
						using Member = typename std::remove_cvref_t<decltype(a_fields)>::ValueType;
						if constexpr (std::same_as<Member, std::string>)
							edits.erase(a_fields.key);
					}(),
						...);
				},
					kSchema.fields);
			}
			// A finished drag has no diff; deferred keys still flush.
			if (edits.empty() && !(flush && (_posted || !_unsaved.empty())))
				return false;
			for (const auto& [key, value] : edits)
				_pending.values.insert_or_assign(key, value);
			if (flush)
				_pending.persist = Persist::kNow;
			return !std::exchange(_posted, true);
		}

		std::optional<PendingCommit> TakePending() override
		{
			std::scoped_lock lock(_mutex);
			if (!std::exchange(_posted, false))
				return std::nullopt;
			return std::exchange(_pending, PendingCommit{});
		}

		void Resync() noexcept override { _version.fetch_add(1, std::memory_order_acq_rel); }

		bool Flush() override { return Write(false); }
		bool Save() override { return Write(true); }

		bool Modified() const override
		{
			std::scoped_lock lock(_mutex);
			return !SerializeDelta(kSchema, Composed(), DefaultSettings()).empty();
		}

		std::vector<std::string_view> RestartPending() const override
		{
			std::vector<std::string_view> pending;
			std::scoped_lock lock(_mutex);
			std::apply([&](const auto&... a_fields) {
				([&] {
					if (a_fields.timing != ApplyTiming::kImmediate && _current.*a_fields.member != _next.*a_fields.member)
						pending.push_back(a_fields.description);
				}(),
					...);
			},
				kSchema.fields);
			return pending;
		}

		void ReloadNext(const toml::table* a_stored) override
		{
			Settings parsed{};
			if (std::string error; a_stored && !Parse(kSchema, *a_stored, parsed, error)) {
				Logger().warn("Stored settings left unchanged for the next launch: {}", error);
				return;
			}
			std::scoped_lock lock(_mutex);
			RestartOnly([&](const auto& a_field) { _next.*a_field.member = parsed.*a_field.member; });
			_version.fetch_add(1, std::memory_order_acq_rel);
		}

	private:
		// Live values with the next-launch values of the restart-only fields.
		Settings Composed() const
		{
			auto composed = _current;
			RestartOnly([&](const auto& a_field) { composed.*a_field.member = _next.*a_field.member; });
			return composed;
		}

		template <class Fn>
		static void RestartOnly(Fn&& a_fn)
		{
			std::apply([&](const auto&... a_fields) {
				([&] {
					if (a_fields.timing != ApplyTiming::kImmediate)
						a_fn(a_fields);
				}(),
					...);
			},
				kSchema.fields);
		}

		bool Write(bool a_all)
		{
			// A comparison variant is never persisted.
			if (shared && liveComparisonActive.load(std::memory_order_acquire))
				return false;
			try {
				std::scoped_lock lock(_mutex);
				if (!a_all && _unsaved.empty())
					return true;
				const auto values = Composed();
				toml::table table;
				std::apply([&](const auto&... a_fields) {
					([&] {
						if ((a_all && a_fields.timing == ApplyTiming::kImmediate) || _unsaved.contains(a_fields.key))
							a_fields.Write(table, values);
					}(),
						...);
				},
					kSchema.fields);
				const std::vector<std::string_view> path(section.begin(), section.end());
				const auto result = feature_config::UpdateOwnedSettings(path, table, feature_config::SectionWrite::kMerge);
				if (!result) {
					Logger().error("Failed to save settings: {}", result.error);
					return false;
				}
				_unsaved.clear();
				return true;
			} catch (const std::exception& error) {
				Logger().error("Failed to save settings: {}", error.what());
				return false;
			}
		}

		Hooks _hooks;
		mutable std::mutex _mutex;
		Settings _current{}, _next{}, _draft{};
		std::set<std::string, std::less<>> _unsaved;
		PendingCommit _pending;
		bool _posted = false;
		std::atomic<std::uint32_t> _version{ 0 };
		std::uint32_t _draftVersion = 0;
	};

	// False when a write failed; the live state has still landed.
	inline bool ApplyPreparedLiveSettings(std::span<PreparedLiveSettings> a_prepared)
	{
		render::CheckFrameBoundary("settings::ApplyPreparedLiveSettings");
		for (auto& value : a_prepared)
			value.swap();
		try {
			for (auto& value : a_prepared) {
				if (value.finalize)
					value.finalize();
			}
		} catch (...) {
			const auto failure = std::current_exception();
			for (auto& value : a_prepared)
				value.swap();
			for (auto& value : a_prepared) {
				try {
					if (value.finalize)
						value.finalize();
				} catch (...) {
				}
			}
			std::rethrow_exception(failure);
		}
		bool persisted = true;
		for (auto& value : a_prepared) {
			if (value.persist && !value.persist())
				persisted = false;
		}
		return persisted;
	}
}
