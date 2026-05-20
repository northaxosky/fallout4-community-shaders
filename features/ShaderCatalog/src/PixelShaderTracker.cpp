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
		};

		std::shared_mutex g_mutex;
		std::unordered_map<ID3D11PixelShader*, Entry> g_map;
		std::atomic<bool> g_enabled{ false };
		std::atomic<std::uint64_t> g_aliasRegistrations{ 0 };
	}

	void TrackPixelShader(ID3D11PixelShader* shader, const Sha1Result& sha, bool alias) noexcept
	{
		if (!g_enabled.load(std::memory_order_acquire) || !shader || Sha1IsZero(sha))
			return;

		try {
			std::unique_lock lock(g_mutex);
			const auto [it, inserted] = g_map.try_emplace(shader);
			if (!inserted) {
				if (it->second.sha.bytes != sha.bytes)
					return;
				if (alias && !it->second.alias) {
					it->second.alias = true;
					g_aliasRegistrations.fetch_add(1, std::memory_order_relaxed);
				}
				return;
			}

			shader->AddRef();
			it->second.sha = sha;
			it->second.alias = alias;
			if (alias)
				g_aliasRegistrations.fetch_add(1, std::memory_order_relaxed);
		} catch (const std::bad_alloc&) {
			// ShaderCatalog is observational; allocation failure must not perturb rendering.
		}
	}

	bool TryGetPixelShaderSha1(ID3D11PixelShader* shader, Sha1Result& sha) noexcept
	{
		if (!g_enabled.load(std::memory_order_acquire) || !shader)
			return false;

		std::shared_lock lock(g_mutex);
		const auto it = g_map.find(shader);
		if (it == g_map.end())
			return false;

		sha = it->second.sha;
		return !Sha1IsZero(sha);
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
		g_aliasRegistrations.store(0, std::memory_order_relaxed);
	}

	Stats GetStats() noexcept
	{
		std::shared_lock lock(g_mutex);
		return Stats{
			static_cast<std::uint64_t>(g_map.size()),
			g_aliasRegistrations.load(std::memory_order_relaxed)
		};
	}
}
