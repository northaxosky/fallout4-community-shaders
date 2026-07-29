#include "Sha256.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace fo4cs::offline::hash
{
	namespace
	{
		constexpr std::size_t kDigestLength = 32;
		constexpr NTSTATUS kInjectedStatus = static_cast<NTSTATUS>(0xC0000001);

		std::unexpected<HashUnavailable> Unavailable(const char* a_stage, NTSTATUS a_status)
		{
			return std::unexpected(
				HashUnavailable{ a_stage, static_cast<std::int32_t>(a_status) });
		}

		// RAII owner with move-only ownership; the handle is closed exactly once.
		class Algorithm
		{
		public:
			explicit Algorithm(BCRYPT_ALG_HANDLE a_handle) noexcept :
				m_handle(a_handle) {}

			~Algorithm()
			{
				if (m_handle != nullptr)
					::BCryptCloseAlgorithmProvider(m_handle, 0);
			}

			Algorithm(Algorithm&& a_other) noexcept :
				m_handle(std::exchange(a_other.m_handle, nullptr)) {}

			Algorithm& operator=(Algorithm&& a_other) noexcept
			{
				if (this != &a_other) {
					if (m_handle != nullptr)
						::BCryptCloseAlgorithmProvider(m_handle, 0);
					m_handle = std::exchange(a_other.m_handle, nullptr);
				}
				return *this;
			}

			Algorithm(const Algorithm&) = delete;
			Algorithm& operator=(const Algorithm&) = delete;

			BCRYPT_ALG_HANDLE Handle() const noexcept { return m_handle; }

		private:
			BCRYPT_ALG_HANDLE m_handle = nullptr;
		};

		// RAII owner: the hash handle is destroyed on every exit path, including a throw.
		class Hash
		{
		public:
			Hash() = default;

			~Hash()
			{
				if (m_handle != nullptr)
					::BCryptDestroyHash(m_handle);
			}

			Hash(const Hash&) = delete;
			Hash& operator=(const Hash&) = delete;

			BCRYPT_HASH_HANDLE* Address() noexcept { return &m_handle; }
			BCRYPT_HASH_HANDLE Handle() const noexcept { return m_handle; }

		private:
			BCRYPT_HASH_HANDLE m_handle = nullptr;
		};

		// Result-returning factory: a failed open is an explicit status, never an exception.
		std::expected<Algorithm, HashUnavailable> OpenAlgorithm()
		{
			BCRYPT_ALG_HANDLE handle = nullptr;
			const auto status = ::BCryptOpenAlgorithmProvider(
				&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
			if (!NT_SUCCESS(status) || handle == nullptr)
				return Unavailable("open-algorithm", status);
			return Algorithm(handle);
		}

		const std::expected<Algorithm, HashUnavailable>& Provider()
		{
			static const std::expected<Algorithm, HashUnavailable> algorithm = OpenAlgorithm();
			return algorithm;
		}

		std::expected<DWORD, HashUnavailable> Property(
			const Algorithm& a_algorithm,
			LPCWSTR a_name,
			const char* a_stage)
		{
			DWORD value = 0;
			DWORD returned = 0;
			const auto status = ::BCryptGetProperty(
				a_algorithm.Handle(),
				a_name,
				reinterpret_cast<PUCHAR>(&value),
				sizeof(value),
				&returned,
				0);
			if (!NT_SUCCESS(status) || returned != sizeof(value) || value == 0)
				return Unavailable(a_stage, status);
			return value;
		}
	}

	Sha256Result Sha256Hex(std::string_view a_bytes, Sha256FaultPoint a_fault)
	{
		const auto& provider = Provider();
		if (!provider)
			return std::unexpected(provider.error());
		if (a_fault == Sha256FaultPoint::kCryptoFailure)
			return Unavailable("injected-provider", kInjectedStatus);

		const auto digestLength = Property(*provider, BCRYPT_HASH_LENGTH, "hash-length");
		if (!digestLength)
			return std::unexpected(digestLength.error());
		if (*digestLength != kDigestLength)
			return Unavailable("hash-length", kInjectedStatus);
		const auto objectLength = Property(*provider, BCRYPT_OBJECT_LENGTH, "object-length");
		if (!objectLength)
			return std::unexpected(objectLength.error());

		if (a_fault == Sha256FaultPoint::kHashObjectAllocation)
			throw std::bad_alloc();
		// Deliberately uncaught: an exhausted hash object buffer is a load failure, not a digest.
		std::vector<unsigned char> object(*objectLength);

		Hash hash;
		auto status = ::BCryptCreateHash(
			provider->Handle(), hash.Address(), object.data(), *objectLength, nullptr, 0, 0);
		if (!NT_SUCCESS(status))
			return Unavailable("create-hash", status);

		std::size_t offset = 0;
		while (offset < a_bytes.size()) {
			const auto chunk = static_cast<ULONG>(std::min<std::size_t>(
				a_bytes.size() - offset,
				static_cast<std::size_t>(std::numeric_limits<ULONG>::max())));
			status = ::BCryptHashData(
				hash.Handle(),
				reinterpret_cast<PUCHAR>(const_cast<char*>(a_bytes.data() + offset)),
				chunk,
				0);
			if (!NT_SUCCESS(status))
				return Unavailable("hash-data", status);
			offset += chunk;
		}

		std::array<unsigned char, kDigestLength> digest{};
		status = ::BCryptFinishHash(
			hash.Handle(), digest.data(), static_cast<ULONG>(digest.size()), 0);
		if (!NT_SUCCESS(status))
			return Unavailable("finish-hash", status);

		static constexpr char kHex[] = "0123456789abcdef";
		std::string text(digest.size() * 2, '0');
		for (std::size_t index = 0; index < digest.size(); ++index) {
			text[index * 2] = kHex[(digest[index] >> 4) & 0x0F];
			text[index * 2 + 1] = kHex[digest[index] & 0x0F];
		}
		return text;
	}
}
