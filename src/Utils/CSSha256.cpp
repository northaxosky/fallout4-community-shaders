#include "Utils/CSSha256.h"

#include "Utils/BCryptHash.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace cs::sha256
{
	namespace
	{
		using bcrypt_hash::Algorithm;
	}

	void Sha256InitOnce()
	{
		bcrypt_hash::Initialize(Algorithm::kSha256);
	}

	Sha256Result Sha256Compute(
		const void* a_data,
		std::size_t a_length) noexcept
	{
		Sha256Result result{};
		if (!bcrypt_hash::Compute(Algorithm::kSha256, a_data, a_length, result.bytes))
			result = {};
		return result;
	}

	bool Sha256ComputeFile(
		const std::filesystem::path& a_path,
		Sha256Result& a_result,
		std::uint64_t& a_length) noexcept
	{
		a_result = {};
		a_length = 0;

		std::ifstream input(a_path, std::ios::binary);
		if (!input.is_open())
			return false;

		bcrypt_hash::Stream stream(Algorithm::kSha256);
		bool success = true;
		std::array<char, 64 * 1024> buffer{};
		while (success) {
			input.read(
				buffer.data(),
				static_cast<std::streamsize>(buffer.size()));
			const auto count = input.gcount();
			if (count > 0) {
				const auto chunkLength = static_cast<std::size_t>(count);
				if (a_length > std::numeric_limits<std::uint64_t>::max() - chunkLength) {
					success = false;
					break;
				}
				a_length += chunkLength;
				success = stream.Update(buffer.data(), chunkLength);
			}
			if (input.bad()) {
				success = false;
				break;
			}
			if (input.eof())
				break;
			if (input.fail()) {
				success = false;
				break;
			}
		}

		success = success && stream.Finish(a_result.bytes);
		if (!success) {
			a_result = {};
			a_length = 0;
		}
		return success;
	}
	bool Sha256IsZero(const Sha256Result& a_result) noexcept
	{
		return std::ranges::all_of(
			a_result.bytes,
			[](std::uint8_t a_byte) { return a_byte == 0; });
	}

	std::string Sha256ToHex(const Sha256Result& a_result)
	{
		static constexpr char kHex[] = "0123456789abcdef";
		std::string result(a_result.bytes.size() * 2, '0');
		for (std::size_t index = 0; index < a_result.bytes.size(); ++index) {
			result[index * 2] =
				kHex[(a_result.bytes[index] >> 4) & 0x0F];
			result[index * 2 + 1] =
				kHex[a_result.bytes[index] & 0x0F];
		}
		return result;
	}
}
