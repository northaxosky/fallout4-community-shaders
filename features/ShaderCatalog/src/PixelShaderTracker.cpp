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
			bool       routeConflict = false;
			std::shared_ptr<RouteCaptureRecordState> routeRecord;
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

	RouteTrackResult TrackRouteLineage(
		ID3D11PixelShader* a_shader,
		const Sha1Result& a_sha,
		const std::shared_ptr<RouteCaptureRecordState>& a_record) noexcept
	{
		if (!g_enabled.load(std::memory_order_acquire)
			|| !a_shader
			|| Sha1IsZero(a_sha)
			|| !a_record)
			return RouteTrackResult::kIgnored;
		try {
			std::unique_lock lock(g_mutex);
			auto found = g_map.find(a_shader);
			if (found == g_map.end()) {
#ifdef FO4CS_SHADER_CATALOG_TESTING
				if (g_failNextAllocation.exchange(
						false, std::memory_order_acq_rel))
					throw std::bad_alloc();
#endif
				auto [inserted, created] =
					g_map.try_emplace(a_shader);
				if (!created)
					return RouteTrackResult::kIgnored;
				a_shader->AddRef();
				g_trackedCount.fetch_add(
					1, std::memory_order_relaxed);
				inserted->second.sha = a_sha;
				found = inserted;
			}
			auto mark = [](
				const std::shared_ptr<RouteCaptureRecordState>& a_value,
				RouteLineageStatus a_status) {
				if (!a_value)
					return;
				std::scoped_lock recordLock(a_value->mutex);
				a_value->observation.lineage.status = a_status;
				a_value->bindReserved = false;
			};
			if (found->second.ambiguousOrigin
				|| found->second.sha.bytes != a_sha.bytes) {
				found->second.ambiguousOrigin = true;
				found->second.routeConflict = true;
				found->second.sha = {};
				mark(
					found->second.routeRecord,
					RouteLineageStatus::kAmbiguous);
				mark(a_record, RouteLineageStatus::kAmbiguous);
				return RouteTrackResult::kAmbiguous;
			}
			if (found->second.routeRecord) {
				if (found->second.routeRecord == a_record)
					return RouteTrackResult::kIgnored;
				found->second.routeConflict = true;
				mark(
					found->second.routeRecord,
					RouteLineageStatus::kDuplicate);
				mark(a_record, RouteLineageStatus::kDuplicate);
				return RouteTrackResult::kDuplicate;
			}
			found->second.routeRecord = a_record;
			return RouteTrackResult::kTracked;
		} catch (const std::bad_alloc&) {
			return RouteTrackResult::kAllocationFailure;
		}
	}

	std::shared_ptr<RouteCaptureRecordState> TryReserveRouteBind(
		ID3D11PixelShader* a_shader) noexcept
	{
		if (!g_enabled.load(std::memory_order_acquire) || !a_shader)
			return {};
		std::shared_lock lock(g_mutex);
		const auto found = g_map.find(a_shader);
		if (found == g_map.end()
			|| found->second.alias
			|| found->second.ambiguousOrigin
			|| found->second.routeConflict
			|| !found->second.routeRecord)
			return {};
		const auto record = found->second.routeRecord;
		std::scoped_lock recordLock(record->mutex);
		if (record->bindReserved
			|| record->observation.lineage.status
				!= RouteLineageStatus::kPendingBind)
			return {};
		record->bindReserved = true;
		return record;
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
