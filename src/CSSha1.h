#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cs::sha1
{
	struct Sha1Result
	{
		std::array<uint8_t, 20> bytes{};
	};

	void Sha1InitOnce();
	Sha1Result Sha1Compute(const void* data, std::size_t len) noexcept;
	bool Sha1IsZero(const Sha1Result& r) noexcept;
	std::string Sha1ToHex(const Sha1Result& r);
	bool Sha1FromHex(const std::string& hex, Sha1Result& out);
}
