#include "SkylightingMath.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": "
					  << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) \
	Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path);
		if (!stream) {
			std::cerr << "FAIL: cannot open " << a_path.string() << '\n';
			++failures;
			return {};
		}
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		return buffer.str();
	}

	std::uint32_t ProducerCell(
		std::uint32_t a_textureID,
		std::uint32_t a_arrayOrigin,
		std::uint32_t a_dimension)
	{
		return (a_textureID - a_arrayOrigin) % a_dimension;
	}

	std::uint32_t ConsumerTexture(
		std::uint32_t a_cellID,
		std::uint32_t a_arrayOrigin,
		std::uint32_t a_dimension)
	{
		return (a_cellID + a_arrayOrigin) % a_dimension;
	}

	void TestAxisRoundTrip(std::uint32_t a_dimension)
	{
		using cs::features::skylighting::NormalizeProbeArrayOrigin;

		struct OriginCase
		{
			std::int64_t origin;
			std::uint32_t normalized;
		};
		const std::array origins{
			OriginCase{ -static_cast<std::int64_t>(a_dimension), 0 },
			OriginCase{
				-static_cast<std::int64_t>(a_dimension / 2),
				a_dimension / 2 },
			OriginCase{ -1, a_dimension - 1 },
			OriginCase{ 0, 0 },
			OriginCase{ 1, 1 },
			OriginCase{
				static_cast<std::int64_t>(a_dimension / 2),
				a_dimension / 2 },
			OriginCase{
				static_cast<std::int64_t>(a_dimension - 1),
				a_dimension - 1 }
		};
		for (const auto [origin, expected] : origins) {
			const auto normalized =
				NormalizeProbeArrayOrigin(origin, a_dimension);
			CHECK(normalized == expected);
			for (std::uint32_t textureID = 0; textureID < a_dimension;
				 ++textureID) {
				const auto cellID =
					ProducerCell(textureID, normalized, a_dimension);
				CHECK(
					ConsumerTexture(cellID, normalized, a_dimension)
					== textureID);
			}
		}
	}

	void TestShaderAndBufferContracts(
		const std::filesystem::path& a_producerPath,
		const std::filesystem::path& a_consumerPath,
		const std::filesystem::path& a_featureHeaderPath,
		const std::filesystem::path& a_sharedDataHeaderPath)
	{
		const auto producer = ReadFile(a_producerPath);
		const auto consumer = ReadFile(a_consumerPath);
		const auto featureHeader = ReadFile(a_featureHeaderPath);
		const auto sharedDataHeader = ReadFile(a_sharedDataHeaderPath);
		if (producer.empty() || consumer.empty() || featureHeader.empty()
			|| sharedDataHeader.empty()) {
			return;
		}

		CHECK(producer.find("uint4 ArrayOrigin;") != std::string::npos);
		CHECK(producer.find("\n\tint4 ArrayOrigin;") == std::string::npos);
		CHECK(
			producer.find("int3(dtid) - ArrayOrigin.xyz")
			!= std::string::npos);
		CHECK(consumer.find("uint4 ArrayOrigin;") != std::string::npos);
		CHECK(consumer.find("\n\t\tint4 ArrayOrigin;") == std::string::npos);
		CHECK(
			consumer.find("(uint3)(cellID + ArrayOrigin.xyz) % ARRAY_DIM")
			!= std::string::npos);
		CHECK(
			featureHeader.find("DirectX::XMUINT4 arrayOrigin{};")
			!= std::string::npos);
		CHECK(
			featureHeader.find("DirectX::XMUINT4 ArrayOrigin{};")
			!= std::string::npos);
		CHECK(
			sharedDataHeader.find("DirectX::XMUINT4    ArrayOrigin{};")
			!= std::string::npos);
	}
}

int main(int a_argc, char* a_argv[])
{
	TestAxisRoundTrip(256);
	TestAxisRoundTrip(256);
	TestAxisRoundTrip(128);

	if (a_argc == 5) {
		TestShaderAndBufferContracts(
			a_argv[1], a_argv[2], a_argv[3], a_argv[4]);
	} else {
		std::cerr << "FAIL: expected producer, consumer, feature header, and shared data paths\n";
		++failures;
	}

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "Skylighting probe mapping tests passed\n";
	return 0;
}
