#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <unordered_map>

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

}
