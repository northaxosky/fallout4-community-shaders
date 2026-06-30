#include "Utils/CSSha1.h"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <Windows.h>
#include <bcrypt.h>

#ifndef NT_SUCCESS
#  define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace cs::sha1
{
	namespace
	{
		std::once_flag g_initOnce;
		BCRYPT_ALG_HANDLE g_alg = nullptr;
		std::atomic<bool> g_ok{ false };

		void DoInit()
		{
			BCRYPT_ALG_HANDLE h = nullptr;
			if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA1_ALGORITHM, nullptr, 0))) {
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
		alignas(8) unsigned char hashObj[512];
		DWORD objLen = 0, cb = 0;
		if (!NT_SUCCESS(BCryptGetProperty(g_alg, BCRYPT_OBJECT_LENGTH,
				reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cb, 0)))
			return out;
		if (objLen > sizeof(hashObj))
			return out;
		if (!NT_SUCCESS(BCryptCreateHash(g_alg, &hash, hashObj, objLen, nullptr, 0, 0)))
			return out;

		if (len > static_cast<std::size_t>(ULONG_MAX)) {
			BCryptDestroyHash(hash);
			return out;
		}
		const auto* p = static_cast<const unsigned char*>(data);
		if (NT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(p), static_cast<ULONG>(len), 0)))
			BCryptFinishHash(hash, out.bytes.data(), static_cast<ULONG>(out.bytes.size()), 0);
		BCryptDestroyHash(hash);
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

	bool Sha1FromHex(const std::string& hex, Sha1Result& out)
	{
		if (hex.size() != 40) return false;
		auto hv = [](char c, uint8_t& v) -> bool {
			if (c >= '0' && c <= '9') { v = static_cast<uint8_t>(c - '0'); return true; }
			if (c >= 'a' && c <= 'f') { v = static_cast<uint8_t>(10 + c - 'a'); return true; }
			if (c >= 'A' && c <= 'F') { v = static_cast<uint8_t>(10 + c - 'A'); return true; }
			return false;
		};
		for (std::size_t i = 0; i < 20; ++i) {
			uint8_t hi = 0, lo = 0;
			if (!hv(hex[i * 2], hi) || !hv(hex[i * 2 + 1], lo)) return false;
			out.bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
		}
		return !Sha1IsZero(out);
	}
}
