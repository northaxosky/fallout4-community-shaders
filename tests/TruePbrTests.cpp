#include "Check.h"
#include "Format/BgsmV100.h"
#include "Format/PbrValidation.h"

#include <bit>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

// Fixtures and expected.tsv come from the reference writer and reader (tests/data/truepbr/generator).
namespace
{
	using namespace cs::features::true_pbr;
	using Bytes = std::vector<std::uint8_t>;

	Bytes ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path, std::ios::binary);
		return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	}

	std::string Hex(float a_value)
	{
		return std::format("{:08x}", std::bit_cast<std::uint32_t>(a_value));
	}

	template <class Range>
	std::string Hex(const Range& a_values)
	{
		std::string text;
		for (const auto value : a_values)
			text += (text.empty() ? "" : ",") + Hex(value);
		return text;
	}

	// The reference reader's cell format, rendered from the decoded material.
	std::vector<std::string> Describe(const PbrMaterial& m, const std::vector<std::string>& a_unknownTags)
	{
		std::vector<std::string> cells{
			std::format("tileFlags={}", m.tileFlags),
			"uvOffset=" + Hex(m.uvOffset),
			"uvScale=" + Hex(m.uvScale),
			"alpha=" + Hex(m.alpha),
			std::format("blend={}/{}/{}", m.alphaBlend, m.blendSource, m.blendDestination),
			std::format("alphaTestRef={}", m.alphaTestRef),
			std::format("alphaTest={}", static_cast<int>(m.alphaTest)),
			std::format("refraction={}", static_cast<int>(m.refraction)),
			std::format("refractionFalloff={}", static_cast<int>(m.refractionFalloff)),
			"refractionPower=" + Hex(m.refractionPower),
			std::format("modelSpaceNormals={}", static_cast<int>(m.modelSpaceNormals)),
			std::format("emitEnabled={}", static_cast<int>(m.emitEnabled)),
			"emittanceColor=" + Hex(m.emittanceColor),
			"emittanceMult=" + Hex(m.emittanceMult),
			std::format("externalEmittance={}", static_cast<int>(m.externalEmittance)),
		};
		for (std::size_t slot = 0; slot < kTextureSlots.size(); ++slot)
			cells.push_back(std::format("{}={}", kTextureSlots[slot].name, m.textures[slot]));
		cells.push_back(std::format("payloadVersion={}", m.payloadVersion));
		cells.push_back(std::format("featureFlags={}", m.featureFlags));
		cells.push_back("parameters=" + Hex(m.parameters));
		cells.push_back("resolved=" + Hex(m.ResolvedParameters()));
		cells.push_back("thin=" + (m.thinSurface ? Hex(std::array{ m.thinSurface->subsurfaceBalance, m.thinSurface->frontSpecularWeight, m.thinSurface->backSpecularWeight, m.thinSurface->backRoughnessScale }) : "-"));
		cells.push_back("fuzz=" + (m.fuzz ? Hex(m.fuzz->roughness) + std::format(",{}", static_cast<int>(m.fuzz->roughnessFromDisplacementGreen)) : "-"));
		std::string tags;
		for (const auto& tag : a_unknownTags)
			tags += (tags.empty() ? "" : ",") + tag;
		cells.push_back("unknownTags=" + (tags.empty() ? "-" : tags));
		return cells;
	}

	struct Row
	{
		std::string file;
		std::size_t part2;
		bool ok;
		std::size_t warnings;
		std::string error;
		std::vector<std::string> cells;
	};

	std::vector<Row> ReadExpected(const std::filesystem::path& a_directory)
	{
		std::vector<Row> rows;
		std::ifstream file(a_directory / "expected.tsv");
		for (std::string line; std::getline(file, line);) {
			std::vector<std::string> fields;
			std::stringstream stream(line);
			for (std::string field; std::getline(stream, field, '\t');)
				fields.push_back(field);
			if (fields.size() < 5)
				continue;
			rows.push_back({ fields[0], std::stoull(fields[1]), fields[2] == "ok", std::stoull(fields[3]), fields[4],
				{ fields.begin() + 5, fields.end() } });
		}
		return rows;
	}

	struct Verdict
	{
		std::optional<PbrMaterial> material;  // set when decoded and valid
		std::vector<std::string> errors;
		std::vector<std::string> warnings;
		std::vector<std::string> unknownTags;
	};

	Verdict Check(std::span<const std::uint8_t> a_part2)
	{
		Verdict verdict;
		auto decoded = DecodePart2(a_part2);
		if (!decoded) {
			verdict.errors.push_back(decoded.error());
			return verdict;
		}
		auto validation = Validate(decoded->material);
		verdict.errors = std::move(validation.errors);
		verdict.warnings = std::move(validation.warnings);
		verdict.unknownTags = std::move(decoded->unknownTags);
		if (verdict.errors.empty())
			verdict.material = std::move(decoded->material);
		return verdict;
	}

	void TestFixtures(const std::filesystem::path& a_directory)
	{
		const auto rows = ReadExpected(a_directory);
		CHECK(rows.size() > 30);
		for (const auto& row : rows) {
			const auto bytes = ReadFile(a_directory / row.file);
			CHECK_MSG(bytes.size() > row.part2, row.file);
			if (bytes.size() <= row.part2)
				continue;
			const auto verdict = Check(std::span(bytes).subspan(row.part2));
			if (!row.ok) {
				CHECK_MSG(!verdict.material, row.file + " should be rejected");
				if (verdict.material || row.error == "-")
					continue;
				CHECK_MSG(verdict.errors.size() == 1, row.file + " should break exactly one rule");
				if (verdict.errors.size() == 1) {
					const auto cut = verdict.errors[0].find(" (is ");
					CHECK_MSG(verdict.errors[0].substr(0, cut) == row.error, row.file + ": " + verdict.errors[0]);
				}
				continue;
			}
			CHECK_MSG(verdict.material, row.file + " should be accepted");
			if (!verdict.material)
				continue;
			CHECK_MSG(verdict.warnings.size() == row.warnings, row.file + " warnings");
			const auto actual = Describe(*verdict.material, verdict.unknownTags);
			CHECK_MSG(actual == row.cells, row.file + " differs from the reference reader");
			if (actual != row.cells)
				for (std::size_t i = 0; i < std::min(actual.size(), row.cells.size()); ++i)
					if (actual[i] != row.cells[i])
						std::cerr << "  " << actual[i] << " != " << row.cells[i] << '\n';
		}
	}

	// A cut file must never read past its end or publish.
	void TestTruncation(const std::filesystem::path& a_directory)
	{
		const auto rows = ReadExpected(a_directory);
		for (const std::string_view name : { "minimal.bgsm", "unknown_blocks.bgsm", "coat_all_slots.bgsm" }) {
			const auto row = std::ranges::find(rows, name, &Row::file);
			CHECK(row != rows.end());
			if (row == rows.end())
				continue;
			const auto bytes = ReadFile(a_directory / row->file);
			const auto part2 = std::span(bytes).subspan(row->part2);
			for (std::size_t length = 0; length < part2.size(); ++length) {
					CHECK_MSG(!Check(part2.first(length)).material, std::format("{} cut to {}", name, length));
			}
		}
	}
}

void RunTruePbrTests(const std::filesystem::path& a_directory)
{
	TestFixtures(a_directory);
	TestTruncation(a_directory);
}
