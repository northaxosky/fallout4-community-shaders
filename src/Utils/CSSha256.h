#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cs::sha256
{
	struct Sha256Result
	{
		std::array<std::uint8_t, 32> bytes{};

		auto operator<=>(const Sha256Result&) const = default;
	};

	void Sha256InitOnce();
	Sha256Result Sha256Compute(const void* a_data, std::size_t a_length) noexcept;
	bool Sha256ComputeFile(
		const std::filesystem::path& a_path,
		Sha256Result& a_result,
		std::uint64_t& a_length) noexcept;
	bool Sha256IsZero(const Sha256Result& a_result) noexcept;
	std::string Sha256ToHex(const Sha256Result& a_result);
}
