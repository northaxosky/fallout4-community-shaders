#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include <toml++/toml.hpp>

namespace cs::telemetry
{
	class Sink
	{
	public:
		Sink& Field(std::string_view a_key, std::string_view a_value);
		Sink& Field(std::string_view a_key, std::int64_t a_value);
		Sink& Field(std::string_view a_key, double a_value);
		Sink& Field(std::string_view a_key, bool a_value);
		template <std::integral T>
			requires(!std::same_as<std::remove_cv_t<T>, bool> &&
					 !std::same_as<std::remove_cv_t<T>, std::int64_t>)
		Sink& Field(std::string_view a_key, T a_value)
		{
			if constexpr (std::is_signed_v<T>) {
				return Field(a_key, static_cast<std::int64_t>(a_value));
			} else {
				constexpr auto max = static_cast<std::make_unsigned_t<T>>(
					std::numeric_limits<std::int64_t>::max());
				return Field(a_key,
					static_cast<std::int64_t>(a_value > max ? max : a_value));
			}
		}
		Sink& Dimensions(std::string_view a_key, std::uint32_t a_width,
			std::uint32_t a_height);

		std::string ToLine() const;
		const toml::table& AsTable() const noexcept;

	private:
		std::string _line;
		toml::table _table;
	};

	std::uint64_t CurrentFrame() noexcept;
	void Install();

	namespace pump
	{
		void Tick();
		void RequestDump() noexcept;
		void SetEnabled(bool a_enabled);
		void SetIntervalSeconds(std::uint32_t a_interval);
		bool Enabled() noexcept;
		std::uint32_t IntervalSeconds() noexcept;
	}  // namespace pump
}  // namespace cs::telemetry
