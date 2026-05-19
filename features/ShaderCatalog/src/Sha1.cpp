#include "Sha1.h"

#include <atomic>
#include <mutex>

#include <Windows.h>
#include <bcrypt.h>

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace cs::features::catalog
{
	namespace
	{
		std::once_flag g_initOnce;
		BCRYPT_ALG_HANDLE g_alg = nullptr;
		std::atomic<bool> g_ok{ false };

		void DoInit()
		{
			BCRYPT_ALG_HANDLE h = nullptr;
			// Open reusable provider. BCRYPT_HASH_REUSABLE_FLAG lets us call BCryptHashData/FinishHash
			// repeatedly on a single hash handle, but we don't actually keep a hash handle around;
			// each Compute creates+destroys a transient hash. The provider handle is what matters.
			const auto s = BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
			if (NT_SUCCESS(s)) {
				g_alg = h;
				g_ok.store(true, std::memory_order_release);
			}
		}
	}

	void Sha1InitOnce()
	{
		std::call_once(g_initOnce, &DoInit);
	}

	Sha1Result Sha1Compute(const void* data, std::size_t len) noexcept
	{
		Sha1Result out{};
		if (!g_ok.load(std::memory_order_acquire) || !g_alg)
			return out;

		BCRYPT_HASH_HANDLE hash = nullptr;
		// Stack-allocate the hash object so we don't malloc on the hot path. SHA1 object size is
		// well under 1KB; query at provider-open would be tighter but 512 is conservatively safe.
		alignas(8) unsigned char hashObj[512];

		DWORD objLen = 0, cb = 0;
		// BCryptGetProperty itself is cheap; result is constant for the provider so we could cache,
		// but the syscall overhead is below SHA1 cost over typical PS bytecode and avoids a global.
		if (!NT_SUCCESS(BCryptGetProperty(g_alg, BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cb, 0)))
			return out;
		if (objLen > sizeof(hashObj))
			return out;

		if (!NT_SUCCESS(BCryptCreateHash(g_alg, &hash, hashObj, objLen, nullptr, 0, 0)))
			return out;

		const auto* p = static_cast<const unsigned char*>(data);
		// BCryptHashData takes ULONG; reject implausibly large blobs (>4GB) explicitly rather
		// than silently truncating. DXBC blobs are well under this in practice.
		if (len > static_cast<std::size_t>(ULONG_MAX)) {
			BCryptDestroyHash(hash);
			return out;
		}
		auto remaining = static_cast<ULONG>(len);
		if (NT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(p), remaining, 0))) {
			BCryptFinishHash(hash, out.bytes.data(), static_cast<ULONG>(out.bytes.size()), 0);
		}
		BCryptDestroyHash(hash);
		return out;
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
