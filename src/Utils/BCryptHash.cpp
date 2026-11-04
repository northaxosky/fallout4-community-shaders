#include "Utils/BCryptHash.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <limits>
#include <mutex>
#include <new>

#ifndef NT_SUCCESS
#	define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace cs::bcrypt_hash
{
	namespace
	{
		struct Provider
		{
			std::once_flag once;
			BCRYPT_ALG_HANDLE handle{};
		};

		BCRYPT_ALG_HANDLE Open(Algorithm a_algorithm)
		{
			static Provider providers[2];
			auto& provider = providers[static_cast<std::size_t>(a_algorithm)];
			std::call_once(provider.once, [&] {
				const auto* id = a_algorithm == Algorithm::kSha1 ? BCRYPT_SHA1_ALGORITHM : BCRYPT_SHA256_ALGORITHM;
				BCRYPT_ALG_HANDLE handle = nullptr;
				if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&handle, id, nullptr, 0)))
					provider.handle = handle;
			});
			return provider.handle;
		}
	}

	void Initialize(Algorithm a_algorithm)
	{
		static_cast<void>(Open(a_algorithm));
	}

	Stream::Stream(Algorithm a_algorithm) noexcept :
		_algorithm(a_algorithm)
	{
		const auto algorithm = Open(a_algorithm);
		if (!algorithm)
			return;
		DWORD objectLength = 0;
		DWORD returned = 0;
		if (!NT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &returned, 0)) ||
			objectLength == 0)
			return;
		try {
			_object.resize(objectLength);
		} catch (...) {
			return;
		}
		BCRYPT_HASH_HANDLE hash = nullptr;
		if (NT_SUCCESS(BCryptCreateHash(algorithm, &hash, _object.data(), objectLength, nullptr, 0, 0))) {
			_hash = hash;
			_ok = true;
		}
	}

	Stream::~Stream()
	{
		if (_hash)
			BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(_hash));
	}

	bool Stream::Update(const void* a_data, std::size_t a_length) noexcept
	{
		if (!_ok || a_length > std::numeric_limits<ULONG>::max() || (!a_data && a_length != 0))
			return _ok = false;
		_ok = NT_SUCCESS(BCryptHashData(
			static_cast<BCRYPT_HASH_HANDLE>(_hash),
			const_cast<PUCHAR>(static_cast<const unsigned char*>(a_data)),
			static_cast<ULONG>(a_length),
			0));
		return _ok;
	}

	bool Stream::Finish(std::span<std::uint8_t> a_digest) noexcept
	{
		if (!_ok || a_digest.size() != DigestSize(_algorithm))
			return _ok = false;
		_ok = NT_SUCCESS(BCryptFinishHash(
			static_cast<BCRYPT_HASH_HANDLE>(_hash), a_digest.data(), static_cast<ULONG>(a_digest.size()), 0));
		return _ok;
	}

	bool Compute(
		Algorithm a_algorithm,
		const void* a_data,
		std::size_t a_length,
		std::span<std::uint8_t> a_digest) noexcept
	{
		Stream stream(a_algorithm);
		return stream.Update(a_data, a_length) && stream.Finish(a_digest);
	}
}
