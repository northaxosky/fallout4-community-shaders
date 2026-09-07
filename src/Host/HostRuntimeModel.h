#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>

#include <DearModdingUI/API.h>
#include <toml++/toml.hpp>

namespace cs::host
{
	class StartupLoadSnapshot
	{
	public:
		void Capture(const toml::table& a_root)
		{
			_loads.clear();
			if (const auto* features = a_root["features"].as_table()) {
				for (const auto& [key, node] : *features) {
					if (const auto* feature = node.as_table())
						_loads.emplace(std::string(key.str()), (*feature)["load"].value_or(false));
				}
			}
		}

		[[nodiscard]] bool RequiresRestart(std::string_view a_key, bool a_requested) const
		{
			const auto found = _loads.find(std::string(a_key));
			const bool atStartup = found != _loads.end() && found->second;
			return a_requested != atStartup;
		}

	private:
		std::unordered_map<std::string, bool> _loads;
	};

	enum class FrameDemandAction : std::uint8_t
	{
		kNone,
		kRequest,
		kRelease
	};

	class FrameDemandTracker
	{
	public:
		[[nodiscard]] FrameDemandAction Next(bool a_wanted) const noexcept
		{
			if (a_wanted == _requested)
				return FrameDemandAction::kNone;
			return a_wanted ?
				FrameDemandAction::kRequest :
				FrameDemandAction::kRelease;
		}

		void Complete(FrameDemandAction a_action, bool a_succeeded) noexcept
		{
			if (!a_succeeded)
				return;
			if (a_action == FrameDemandAction::kRequest)
				_requested = true;
			else if (a_action == FrameDemandAction::kRelease)
				_requested = false;
		}

		[[nodiscard]] bool Requested() const noexcept { return _requested; }

	private:
		bool _requested{};
	};

	class DialogSubmissionTracker
	{
	public:
		struct Outcome
		{
			std::uint64_t submission{};
			bool accepted{};
			std::string error;
		};

		[[nodiscard]] bool ShouldExecute(std::uint64_t a_submission) const noexcept
		{
			return a_submission != 0 &&
				a_submission != _lastResolved &&
				!_pending;
		}

		void RecordOutcome(
			std::uint64_t a_submission,
			bool a_accepted,
			std::string a_error)
		{
			if (a_submission == 0)
				return;
			_pending = Outcome{
				a_submission,
				a_accepted,
				std::move(a_error)
			};
		}

		[[nodiscard]] const Outcome* Pending(
			std::uint64_t a_submission) const noexcept
		{
			if (!_pending || _pending->submission != a_submission)
				return nullptr;
			return &*_pending;
		}

		void ResolutionSucceeded(std::uint64_t a_submission) noexcept
		{
			if (!_pending || _pending->submission != a_submission)
				return;
			_lastResolved = a_submission;
			_pending.reset();
		}

		void Reset() noexcept
		{
			_lastResolved = 0;
			_pending.reset();
		}

	private:
		std::uint64_t _lastResolved{};
		std::optional<Outcome> _pending;
	};

	class DialogCallRetry
	{
	public:
		[[nodiscard]] bool Ready(std::uint64_t a_frame) const noexcept
		{
			return a_frame >= _nextFrame;
		}

		[[nodiscard]] bool Failed(
			DMUI_Result a_result,
			std::uint64_t a_frame) noexcept
		{
			const bool changed = !_failure || *_failure != a_result;
			_failure = a_result;
			_nextFrame = a_frame + 60;
			return changed;
		}

		void Succeeded() noexcept
		{
			_failure.reset();
			_nextFrame = 0;
		}

		[[nodiscard]] static constexpr bool HandleLost(DMUI_Result a_result) noexcept
		{
			return a_result == DMUI_RESULT_STALE_HANDLE ||
				a_result == DMUI_RESULT_CLIENT_NOT_FOUND;
		}

	private:
		std::uint64_t _nextFrame{};
		std::optional<DMUI_Result> _failure;
	};

	enum class ImageImportFailure : std::uint8_t
	{
		kNone,
		kTransient,
		kPermanent
	};

	[[nodiscard]] constexpr bool ShouldImportImage(
		bool a_hasHandle,
		ImageImportFailure a_previousFailure,
		bool a_sourceChanged,
		bool a_querySucceeded,
		bool a_imageReady,
		bool a_retryDue) noexcept
	{
		return a_sourceChanged ||
			(a_hasHandle && (!a_querySucceeded || !a_imageReady)) ||
			(!a_hasHandle &&
				(a_previousFailure == ImageImportFailure::kNone ||
					(a_previousFailure == ImageImportFailure::kTransient &&
						a_retryDue)));
	}

	struct LegacyHotkeySeed
	{
		bool present{};
		bool valid{};
		std::string chord;
		std::string error;
	};

	[[nodiscard]] inline LegacyHotkeySeed ParseLegacyKeyboardHotkey(
		const toml::node* a_node)
	{
		if (!a_node)
			return {};

		auto keyName = [](std::uint32_t a_vk) -> std::string {
			if (a_vk >= 0x70 && a_vk <= 0x7B)
				return "F" + std::to_string(a_vk - 0x70 + 1);
			if ((a_vk >= 'A' && a_vk <= 'Z') ||
				(a_vk >= '0' && a_vk <= '9'))
				return std::string(1, static_cast<char>(a_vk));
			return {};
		};

		LegacyHotkeySeed result{ .present = true };
		bool shift{};
		bool ctrl{};
		bool alt{};
		bool unbound{};
		std::uint32_t key{};
		auto consume = [&](std::int64_t a_value) {
			if (a_value < 0 || a_value > 0xFFFF) {
				result.error = "contains a non-keyboard binding";
				return false;
			}
			const auto vk = static_cast<std::uint32_t>(a_value);
			if (vk == 0) {
				if (key || shift || ctrl || alt) {
					result.error = "mixes the unbound value with other keys";
					return false;
				}
				unbound = true;
				return true;
			}
			if (unbound) {
				result.error = "mixes the unbound value with other keys";
				return false;
			}
			if (vk == 0x10 || vk == 0x11 || vk == 0x12) {
				bool* modifier =
					vk == 0x10 ? &shift :
					vk == 0x11 ? &ctrl :
						&alt;
				if (*modifier) {
					result.error = "contains a duplicate modifier";
					return false;
				}
				*modifier = true;
				return true;
			}
			if (key || keyName(vk).empty()) {
				result.error = key ?
					"contains more than one non-modifier key" :
					"contains an unsupported keyboard key";
				return false;
			}
			key = vk;
			return true;
		};

		if (const auto value = a_node->value<std::int64_t>()) {
			if (!consume(*value))
				return result;
			if (*value == 0) {
				result.valid = true;
				result.chord = "None";
				return result;
			}
		} else if (const auto* values = a_node->as_array()) {
			if (values->empty()) {
				result.error = "is an empty key array";
				return result;
			}
			for (const auto& element : *values) {
				const auto integer = element.value<std::int64_t>();
				if (!integer) {
					result.error = "must contain only integers";
					return result;
				}
				if (!consume(*integer))
					return result;
			}
		} else {
			result.error = "must be an integer or integer array";
			return result;
		}

		if (unbound) {
			result.valid = true;
			result.chord = "None";
			return result;
		}
		if (!key) {
			result.error = "does not contain a supported non-modifier key";
			return result;
		}
		if (shift)
			result.chord += "Shift+";
		if (ctrl)
			result.chord += "Ctrl+";
		if (alt)
			result.chord += "Alt+";
		result.chord += keyName(key);
		result.valid = true;
		return result;
	}

	[[nodiscard]] constexpr std::string_view ResolveHotkeySeed(
		std::string_view a_configured,
		bool a_configuredExplicitly,
		std::string_view a_legacy) noexcept
	{
		return !a_configuredExplicitly && !a_legacy.empty() ?
			a_legacy :
			a_configured;
	}
}
