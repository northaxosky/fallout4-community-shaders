#include "Check.h"
#include "Format/BgsmV100.h"
#include "MaterialRegistry.h"

#include <bit>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>

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

	struct Outcome
	{
		std::expected<Ingested, Rejection> result;
		std::vector<std::string> unknownTags;
	};

	// The binary route: decode Part 2, then publish.
	Outcome Load(MaterialRegistry& a_registry, std::string_view a_name, std::span<const std::uint8_t> a_part2)
	{
		auto decoded = DecodePart2(a_part2);
		if (!decoded)
			return { std::unexpected(Rejection{ { decoded.error() } }), {} };
		return { a_registry.Ingest(a_name, std::move(decoded->material)), std::move(decoded->unknownTags) };
	}

	std::string Stem(const std::string& a_file)
	{
		return "materials\\test\\" + a_file;
	}

	void TestFixtures(const std::filesystem::path& a_directory)
	{
		const auto rows = ReadExpected(a_directory);
		CHECK(rows.size() > 30);
		MaterialRegistry registry;
		for (const auto& row : rows) {
			const auto bytes = ReadFile(a_directory / row.file);
			CHECK_MSG(bytes.size() > row.part2, row.file);
			if (bytes.size() <= row.part2)
				continue;
			CHECK_MSG(ReadBgsmVersion(bytes) == kBgsmVersion, row.file + " version");
			const auto part2 = std::span(bytes).subspan(row.part2);
			auto loaded = Load(registry, Stem(row.file), part2);
			if (!row.ok) {
				CHECK_MSG(!loaded.result, row.file + " should be rejected");
				if (loaded.result || row.error == "-")
					continue;
				const auto& errors = loaded.result.error().errors;
				CHECK_MSG(errors.size() == 1, row.file + " should break exactly one rule");
				if (errors.size() == 1) {
					const auto cut = errors[0].find(" (is ");
					CHECK_MSG(errors[0].substr(0, cut) == row.error, row.file + ": " + errors[0]);
				}
				continue;
			}
			CHECK_MSG(loaded.result, row.file + " should be accepted");
			if (!loaded.result)
				continue;
			CHECK_MSG(loaded.result->warnings.size() == row.warnings, row.file + " warnings");
			const auto actual = Describe(loaded.result->record->material, loaded.unknownTags);
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
				MaterialRegistry registry;
				CHECK_MSG(!Load(registry, "materials\\x.bgsm", part2.first(length)).result, std::format("{} cut to {}", name, length));
				CHECK(registry.Count() == 0);
			}
		}
	}

	void TestIdentity(const std::filesystem::path& a_directory)
	{
		const auto rows = ReadExpected(a_directory);
		const auto bytes = ReadFile(a_directory / "minimal.bgsm");
		const auto part2 = std::span(bytes).subspan(std::ranges::find(rows, "minimal.bgsm", &Row::file)->part2);

		// Every spelling of one file, including the absolute build path, names one record.
		MaterialRegistry registry;
		RecordId id = 0;
		for (const auto* spelling : { "DATA\\MATERIALS\\Test\\Rock.BGSM", "Data/Materials/test/rock.bgsm", "C:\\Build\\Data\\Materials\\TEST\\rock.bgsm", "materials\\test\\rock.bgsm" }) {
			const auto name = CanonicalName(spelling);
			CHECK(name && *name == "materials\\test\\rock.bgsm");
			const auto loaded = Load(registry, *name, part2);
			CHECK(loaded.result);
			if (!loaded.result)
				continue;
			if (id == 0)
				id = loaded.result->record->id;
			CHECK(loaded.result->record->id == id);
		}
		CHECK(registry.Count() == 1);
		CHECK(registry.Find("MATERIALS\\TEST\\ROCK.BGSM") == registry.Get(id));
		CHECK(!CanonicalName("textures\\rock.dds"));

		// An edited file keeps the published record and its id.
		const auto edited = ReadFile(a_directory / "nondefault.bgsm");
		const auto editedPart2 = std::span(edited).subspan(std::ranges::find(rows, "nondefault.bgsm", &Row::file)->part2);
		const auto changed = Load(registry, "materials\\test\\rock.bgsm", editedPart2);
		CHECK(changed.result && changed.result->outcome == IngestOutcome::kChangedOnDisk);
		CHECK(changed.result && changed.result->record->id == id);
		CHECK(registry.Get(id)->material.uvScale[0] == 1.0f);

		// A rejected edit of a published file leaves it alone.
		const auto broken = ReadFile(a_directory / "invalid" / "missing_rmaos.bgsm");
		CHECK(!Load(registry, "materials\\test\\rock.bgsm", std::span(broken).subspan(rows.front().part2)).result);
		CHECK(registry.Count() == 1 && registry.Get(id));
	}

	void TestConcurrentIngest(const std::filesystem::path& a_directory)
	{
		const auto rows = ReadExpected(a_directory);
		const auto bytes = ReadFile(a_directory / "minimal.bgsm");
		const auto part2 = std::span(bytes).subspan(std::ranges::find(rows, "minimal.bgsm", &Row::file)->part2);
		for (int round = 0; round < 50; ++round) {
			MaterialRegistry registry;
			std::array<RecordId, 8> ids{};
			std::vector<std::jthread> threads;
			for (std::size_t i = 0; i < ids.size(); ++i)
				threads.emplace_back([&, i] {
					const auto name = CanonicalName(i % 2 ? "Data\\Materials\\a.bgsm" : "C:\\Build\\Data\\MATERIALS\\a.bgsm");
					if (const auto loaded = Load(registry, *name, part2); loaded.result)
						ids[i] = loaded.result->record->id;
				});
			threads.clear();
			CHECK(registry.Count() == 1);
			CHECK(std::ranges::all_of(ids, [&](RecordId a_id) { return a_id == ids[0] && a_id != 0; }));
		}
	}
}

void RunTruePbrTests(const std::filesystem::path& a_directory)
{
	TestFixtures(a_directory);
	TestTruncation(a_directory);
	TestIdentity(a_directory);
	TestConcurrentIngest(a_directory);
}
