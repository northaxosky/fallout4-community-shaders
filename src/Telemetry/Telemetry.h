#pragma once

#include <cstdint>
#include <string>
#include <string_view>

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
		Sink& Dimensions(std::string_view a_key, std::uint32_t a_width, std::uint32_t a_height);

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
		void DumpAll();
		void SetEnabled(bool a_enabled);
		void SetIntervalFrames(std::uint32_t a_interval);
		bool Enabled() noexcept;
		std::uint32_t IntervalFrames() noexcept;
	}
}
