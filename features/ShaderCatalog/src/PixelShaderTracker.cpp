#include "PixelShaderTracker.h"

#include <d3d11.h>

#include <atomic>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <unordered_map>

namespace cs::features::catalog::shader_tracker
{
	namespace
	{
		struct Entry
		{
			Sha1Result sha{};
			bool       alias = false;
			bool       ambiguousOrigin = false;
		};

		std::shared_mutex g_mutex;
		std::unordered_map<ID3D11PixelShader*, Entry> g_map;
		std::atomic<bool> g_enabled{ false };
		std::atomic<std::uint64_t> g_trackedCount{ 0 };
		std::atomic<std::uint64_t> g_aliasRegistrations{ 0 };
#ifdef FO4CS_SHADER_CATALOG_TESTING
		std::atomic<bool> g_failNextAllocation{ false };
#endif
	}

	TrackResult TrackPixelShader(
		ID3D11PixelShader* shader,
		const Sha1Result& sha,
		bool alias) noexcept
	{
		if (!g_enabled.load(std::memory_order_acquire) || !shader || Sha1IsZero(sha))
			return TrackResult::kIgnored;

		try {
			std::unique_lock lock(g_mutex);
#ifdef FO4CS_SHADER_CATALOG_TESTING
			if (g_failNextAllocation.exchange(false, std::memory_order_acq_rel))
				throw std::bad_alloc();
#endif
			const auto [it, inserted] = g_map.try_emplace(shader);
			if (!inserted) {
				if (it->second.sha.bytes != sha.bytes) {
					it->second.ambiguousOrigin = true;
					it->second.sha = {};
					return TrackResult::kAmbiguousOrigin;
				}
				if (alias && !it->second.alias) {
					it->second.alias = true;
					g_aliasRegistrations.fetch_add(1, std::memory_order_relaxed);
					return TrackResult::kUpdated;
				}
				return it->second.ambiguousOrigin
					? TrackResult::kAmbiguousOrigin
					: TrackResult::kIgnored;
			}

			shader->AddRef();
			it->second.sha = sha;
			it->second.alias = alias;
			g_trackedCount.fetch_add(1, std::memory_order_relaxed);
			if (alias)
				g_aliasRegistrations.fetch_add(1, std::memory_order_relaxed);
			return TrackResult::kTracked;
		} catch (const std::bad_alloc&) {
			return TrackResult::kAllocationFailure;
		}
	}

	bool TryGetPixelShader(ID3D11PixelShader* shader, Lookup& result) noexcept
	{
		if (!g_enabled.load(std::memory_order_acquire) || !shader)
			return false;

		std::shared_lock lock(g_mutex);
		const auto it = g_map.find(shader);
		if (it == g_map.end())
			return false;

		result.sha = it->second.sha;
		result.alias = it->second.alias;
		result.ambiguousOrigin = it->second.ambiguousOrigin;
		return result.ambiguousOrigin || !Sha1IsZero(result.sha);
	}

	void SetEnabled(bool enabled) noexcept
	{
		g_enabled.store(enabled, std::memory_order_release);
		if (!enabled)
			Clear();
	}

	void Clear() noexcept
	{
		std::unique_lock lock(g_mutex);
		for (auto& [shader, entry] : g_map) {
			(void)entry;
			shader->Release();
		}
		g_map.clear();
		g_trackedCount.store(0, std::memory_order_relaxed);
		g_aliasRegistrations.store(0, std::memory_order_relaxed);
	}

	Stats GetStats() noexcept
	{
		return Stats{
			g_trackedCount.load(std::memory_order_relaxed),
			g_aliasRegistrations.load(std::memory_order_relaxed)
		};
	}

#ifdef FO4CS_SHADER_CATALOG_TESTING
	void FailNextAllocationForTesting() noexcept
	{
		g_failNextAllocation.store(true, std::memory_order_release);
	}
#endif
}
