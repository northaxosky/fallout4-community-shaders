#include "Render/StockShaderTable.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace cs::engine
{
	namespace
	{
		void Require(bool a_condition, const std::string& a_message)
		{
			if (!a_condition)
				throw std::runtime_error(a_message);
		}

		bool IsHex(std::string_view a_text, std::size_t a_length)
		{
			return a_text.size() == a_length && a_text.find_first_not_of("0123456789abcdef") == a_text.npos;
		}

		GameRuntime ParseRuntime(std::string_view a_build)
		{
			for (const auto [prefix, runtime] : { std::pair{ "OG-", GameRuntime::kOG }, std::pair{ "NG-", GameRuntime::kNG },
					 std::pair{ "AE-", GameRuntime::kAE } }) {
				if (a_build.starts_with(prefix))
					return runtime;
			}
			throw std::runtime_error("Unknown identity table runtime: " + std::string(a_build));
		}

		ShaderStage ParseStage(const std::string& a_stage)
		{
			if (a_stage == "vertex")
				return ShaderStage::kVertex;
			if (a_stage == "pixel")
				return ShaderStage::kPixel;
			if (a_stage == "compute")
				return ShaderStage::kCompute;
			throw std::runtime_error("Unknown stage: " + a_stage);
		}

		ShaderInjectionTarget ParseTarget(const std::string& a_target)
		{
			for (const auto& target : GetShaderInjectionTargets()) {
				if (target.name == a_target)
					return target.id;
			}
			return ShaderInjectionTarget::kCount;
		}
	}

	ShaderFamilyDescriptor StockShaderRow::Family() const
	{
		return {
			.target = target,
			.stage = stage,
			.descriptor = descriptor,
			.nativeName = nativeName,
			.nativeClassName = nativeClassName,
			.nativeSourceGroup = nativeSourceGroup,
			.forceEarlyDepthStencil = forceEarlyDepthStencil,
			.nativeMacros = nativeMacros
		};
	}

	StockShaderTable ParseStockShaderTable(std::string_view a_text)
	{
		std::istringstream file{ std::string(a_text) };
		std::string line;
		Require(static_cast<bool>(std::getline(file, line)) && line.starts_with("# "), "Missing identity table header");
		std::istringstream header(line.substr(2));
		StockShaderTable table;
		for (std::string token; header >> token;) {
			const auto separator = token.find('=');
			Require(separator != token.npos && table.header.emplace(token.substr(0, separator), token.substr(separator + 1)).second,
				"Invalid identity table header");
		}
		table.runtime = ParseRuntime(table.header.at("runtime"));
		table.inheritsTargets = std::stoull(table.header.at("excluded_inherited")) != 0;

		std::set<std::tuple<ShaderInjectionTarget, ShaderStage, std::uint32_t, std::uint32_t>> keys;
		while (std::getline(file, line)) {
			// Stray carriage returns from a CRLF checkout.
			if (line.ends_with('\r'))
				line.pop_back();
			StockShaderRow row;
			std::string stage;
			unsigned early = 0;
			unsigned macroCount = 0;
			std::istringstream input(line);
			Require(static_cast<bool>(input >> row.targetName >> stage >> std::hex >> row.descriptor >> std::dec >> early >> row.expected >> row.ordinal >>
									  std::quoted(row.nativeName) >> std::quoted(row.nativeClassName) >> std::quoted(row.nativeSourceGroup) >> macroCount),
				"Malformed identity row: " + line);
			for (unsigned i = 0; i < macroCount; ++i) {
				std::string name;
				std::string value;
				Require(static_cast<bool>(input >> std::quoted(name) >> std::quoted(value)) &&
							row.nativeMacros.emplace(name, value).second,
					"Invalid native macros: " + line);
			}
			input >> std::ws;
			Require(input.eof() && early <= 1 && IsHex(row.expected, 40), "Invalid identity row: " + line);
			row.target = ParseTarget(row.targetName);
			row.stage = ParseStage(stage);
			row.forceEarlyDepthStencil = early != 0;
			Require(keys.emplace(row.target, row.stage, row.descriptor, row.ordinal).second, "Duplicate identity row: " + line);
			table.rows.push_back(std::move(row));
		}
		Require(file.eof() && !table.rows.empty() && table.rows.size() == std::stoull(table.header.at("rows")), "Identity row count mismatch");
		Require(static_cast<std::size_t>(std::ranges::count_if(table.rows, [](const auto& row) { return row.nativeName.empty(); })) ==
					std::stoull(table.header.at("rows_unnamed")),
			"Unnamed identity row count mismatch");
		return table;
	}
}
