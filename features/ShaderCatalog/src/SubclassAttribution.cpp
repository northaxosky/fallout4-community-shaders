#include "SubclassAttribution.h"

#include "PCH.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "CatalogDB.h"
#include "PixelShaderTracker.h"
#include "Render/ShaderSubclassHooks.h"

namespace cs::features::catalog::subclass_attribution
{
	namespace
	{
		std::atomic<std::uint64_t> g_mapAttributions{ 0 };
		std::atomic<std::size_t> g_pixelShadersOffset{ 0 };

		using PixelShaderMap =
			RE::BSShaderTechniqueIDMap::MapType<RE::BSGraphics::PixelShader*>;

		void ObserveSetupTechnique(
			void* a_shader,
			const char* a_subclassName,
			std::uint32_t a_techniqueBits) noexcept
		{
			auto lease = CatalogDB::Get().TryAcquireProducerLease();
			if (!lease || !a_shader || !a_subclassName)
				return;

			auto* map = reinterpret_cast<PixelShaderMap*>(
				reinterpret_cast<std::byte*>(a_shader)
				+ g_pixelShadersOffset.load(std::memory_order_acquire));
			RE::BSGraphics::PixelShader probe{};
			probe.id = a_techniqueBits;
			const auto it = map->find(&probe);
			if (it == map->end() || !*it || !(*it)->shader)
				return;

			shader_tracker::Lookup lookup;
			auto* d3dShader =
				reinterpret_cast<ID3D11PixelShader*>((*it)->shader);
			if (!shader_tracker::TryGetPixelShader(d3dShader, lookup))
				return;
			if (lookup.ambiguousOrigin) {
				CatalogDB::Get().RecordHookObserverGap();
				return;
			}

			g_mapAttributions.fetch_add(1, std::memory_order_relaxed);
			CatalogDB::Get().EnqueueAttribution(
				lookup.sha,
				a_subclassName,
				a_techniqueBits,
				AttributionKind::kTechniqueMapAssociation,
				lookup.alias
					? AttributionObjectKind::kReplacementUnknown
					: AttributionObjectKind::kStock,
				&lease);
		}
	}

	bool Register(std::size_t a_pixelShadersOffset) noexcept
	{
		g_pixelShadersOffset.store(
			a_pixelShadersOffset, std::memory_order_release);
		return cs::engine::RegisterShaderSubclassSetupObserver(
			&ObserveSetupTechnique);
	}

	RuntimeStats GetRuntimeStats() noexcept
	{
		return {
			cs::engine::GetShaderSubclassHookRuntimeStats()
				.setupTechniqueCalls,
			g_mapAttributions.load(std::memory_order_relaxed)
		};
	}
}
