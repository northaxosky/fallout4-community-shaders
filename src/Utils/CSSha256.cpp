#include "Utils/CSSha256.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <vector>

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace cs::sha256
{
	namespace
	{
		std::once_flag g_initOnce;
		BCRYPT_ALG_HANDLE g_algorithm = nullptr;
		std::atomic_bool g_ready{ false };

		void Initialize()
		{
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			if (NT_SUCCESS(BCryptOpenAlgorithmProvider(
					&algorithm,
					BCRYPT_SHA256_ALGORITHM,
					nullptr,
					0))) {
				g_algorithm = algorithm;
				g_ready.store(true, std::memory_order_release);
			}
		}

		bool CreateHash(
			BCRYPT_HASH_HANDLE& a_hash,
			std::vector<unsigned char>& a_object) noexcept
		{
			if (!g_ready.load(std::memory_order_acquire) || !g_algorithm)
				return false;

			DWORD objectLength = 0;
			DWORD returned = 0;
			if (!NT_SUCCESS(BCryptGetProperty(
					g_algorithm,
					BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(&objectLength),
					sizeof(objectLength),
					&returned,
					0))
				|| objectLength == 0) {
				return false;
			}

			try {
				a_object.resize(objectLength);
			} catch (...) {
				return false;
			}
			return NT_SUCCESS(BCryptCreateHash(
				g_algorithm,
				&a_hash,
				a_object.data(),
				objectLength,
				nullptr,
				0,
				0));
		}

		bool FinishHash(
			BCRYPT_HASH_HANDLE a_hash,
			Sha256Result& a_result) noexcept
		{
			return NT_SUCCESS(BCryptFinishHash(
				a_hash,
				a_result.bytes.data(),
				static_cast<ULONG>(a_result.bytes.size()),
				0));
		}

		bool HashChunk(
			BCRYPT_HASH_HANDLE a_hash,
			const void* a_data,
			std::size_t a_length) noexcept
		{
			if (a_length > static_cast<std::size_t>(
					std::numeric_limits<ULONG>::max())) {
				return false;
			}
			return NT_SUCCESS(BCryptHashData(
				a_hash,
				const_cast<PUCHAR>(
					static_cast<const unsigned char*>(a_data)),
				static_cast<ULONG>(a_length),
				0));
		}
	}

	void Sha256InitOnce()
	{
		std::call_once(g_initOnce, &Initialize);
	}

	Sha256Result Sha256Compute(
		const void* a_data,
		std::size_t a_length) noexcept
	{
		Sha256Result result{};
		if (!a_data && a_length != 0)
			return result;

		Sha256InitOnce();
		BCRYPT_HASH_HANDLE hash = nullptr;
		std::vector<unsigned char> object;
		if (!CreateHash(hash, object))
			return result;

		const bool success =
			HashChunk(hash, a_data, a_length)
			&& FinishHash(hash, result);
		BCryptDestroyHash(hash);
		if (!success)
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
		Sha256InitOnce();

		std::ifstream input(a_path, std::ios::binary);
		if (!input.is_open())
			return false;

		BCRYPT_HASH_HANDLE hash = nullptr;
		std::vector<unsigned char> object;
		if (!CreateHash(hash, object))
			return false;

		bool success = true;
		std::array<char, 64 * 1024> buffer{};
		while (success) {
			input.read(
				buffer.data(),
				static_cast<std::streamsize>(buffer.size()));
			const auto count = input.gcount();
			if (count > 0) {
				const auto chunkLength = static_cast<std::size_t>(count);
				if (a_length
					> std::numeric_limits<std::uint64_t>::max()
						- chunkLength) {
					success = false;
					break;
				}
				a_length += chunkLength;
				success = HashChunk(hash, buffer.data(), chunkLength);
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

		success = success && FinishHash(hash, a_result);
		BCryptDestroyHash(hash);
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
