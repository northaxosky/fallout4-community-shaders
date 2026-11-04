#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cs::bcrypt_hash
{
	enum class Algorithm : std::uint8_t
	{
		kSha1,
		kSha256
	};

	[[nodiscard]] constexpr std::size_t DigestSize(Algorithm a_algorithm) noexcept
	{
		return a_algorithm == Algorithm::kSha1 ? 20 : 32;
	}

	// Opens the algorithm provider once; safe to call from any thread.
	void Initialize(Algorithm a_algorithm);

	// Incremental hash; every method fails closed once any step has failed.
	class Stream
	{
	public:
		explicit Stream(Algorithm a_algorithm) noexcept;
		~Stream();
		Stream(const Stream&) = delete;
		Stream& operator=(const Stream&) = delete;

		[[nodiscard]] bool Update(const void* a_data, std::size_t a_length) noexcept;
		// Writes DigestSize(algorithm) bytes into a_digest.
		[[nodiscard]] bool Finish(std::span<std::uint8_t> a_digest) noexcept;

	private:
		void* _hash{};
		Algorithm _algorithm;
		std::vector<unsigned char> _object;
		bool _ok{ false };
	};

	[[nodiscard]] bool Compute(
		Algorithm a_algorithm,
		const void* a_data,
		std::size_t a_length,
		std::span<std::uint8_t> a_digest) noexcept;
}
