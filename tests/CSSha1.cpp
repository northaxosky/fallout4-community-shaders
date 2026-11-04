#include "CSSha1.h"

#include "Utils/BCryptHash.h"

#include <algorithm>

namespace cs::sha1
{
	void Sha1InitOnce()
	{
		bcrypt_hash::Initialize(bcrypt_hash::Algorithm::kSha1);
	}

	Sha1Result Sha1Compute(const void* data, std::size_t len) noexcept
	{
		Sha1Result out{};
		if (!bcrypt_hash::Compute(bcrypt_hash::Algorithm::kSha1, data, len, out.bytes))
			out = {};
		return out;
	}

	bool Sha1IsZero(const Sha1Result& r) noexcept
	{
		return std::all_of(r.bytes.begin(), r.bytes.end(), [](std::uint8_t b) { return b == 0; });
	}

	std::string Sha1ToHex(const Sha1Result& r)
	{
		static constexpr char kHex[] = "0123456789abcdef";
		std::string s(40, '0');
		for (std::size_t i = 0; i < r.bytes.size(); ++i) {
			s[i * 2 + 0] = kHex[(r.bytes[i] >> 4) & 0xF];
			s[i * 2 + 1] = kHex[r.bytes[i] & 0xF];
		}
		return s;
	}
}