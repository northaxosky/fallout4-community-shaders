#include "SubclassHooks.h"

#include "PCH.h"

#include "CatalogDB.h"
#include "Log.h"
#include "PixelShaderTracker.h"
#include "SubclassContext.h"

#include "RE/B/BSBloodSplatterShader.h"
#include "RE/B/BSDFCompositeShader.h"
#include "RE/B/BSDFLightShader.h"
#include "RE/B/BSDFPrePassShader.h"
#include "RE/B/BSDistantTreeShader.h"
#include "RE/B/BSEffectShader.h"
#include "RE/B/BSFaceCustomizationShader.h"
#include "RE/B/BSLightingShader.h"
#include "RE/B/BSParticleShader.h"
#include "RE/B/BSSkyShader.h"
#include "RE/B/BSUtilityShader.h"
#include "RE/B/BSWaterShader.h"

namespace cs::features::catalog::subclass_hooks
{
	namespace { auto* L = cs::log::Get("cs.feature.catalog"); }

	namespace
	{
		std::atomic<std::uint64_t> g_setupCalls{ 0 };
		std::atomic<std::uint64_t> g_mapAttributions{ 0 };

		void AttributePixelShaderFromMap(RE::BSShader* self, const char* subclassName, std::uint32_t techniqueBits)
		{
			if (!self || !subclassName)
				return;

			RE::BSGraphics::PixelShader probe{};
			probe.id = techniqueBits;
			const auto it = self->pixelShaders.find(&probe);
			if (it == self->pixelShaders.end() || !*it || !(*it)->shader)
				return;

			Sha1Result sha{};
			auto* d3dShader = reinterpret_cast<ID3D11PixelShader*>((*it)->shader);
			if (!shader_tracker::TryGetPixelShaderSha1(d3dShader, sha))
				return;

			g_mapAttributions.fetch_add(1, std::memory_order_relaxed);
			CatalogDB::Get().EnqueueAttribution(sha, subclassName, techniqueBits);
		}

		// One hook type per subclass. Tag carries the human-readable subclass label;
		// `size` is the BSShader vtable slot for ReloadShaders (0x0B). `func` is
		// initialized when stl::write_vfunc patches the slot and returns the original.
		template <class Tag>
		struct ReloadHook
		{
			static constexpr std::size_t size = 0x0B;

			static void thunk(void* self, bool clear)
			{
				context::Scope scope(Tag::Name(), 0);
				func(self, clear);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class Tag>
		struct SetupTechniqueHook
		{
			static constexpr std::size_t size = 0x02;

			static bool thunk(RE::BSShader* self, std::uint32_t techniqueBits)
			{
				g_setupCalls.fetch_add(1, std::memory_order_relaxed);
				context::SetSticky(Tag::Name(), techniqueBits);
				context::Scope scope(Tag::Name(), techniqueBits);
				const bool ok = func(self, techniqueBits);
				if (ok)
					AttributePixelShaderFromMap(self, Tag::Name(), techniqueBits);
				else
					context::ClearSticky();
				return ok;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		InstallStats g_reloadStats{};
		InstallStats g_setupStats{};
		std::once_flag g_once;

		template <class Subclass, class Tag>
		void TryInstallReload()
		{
			++g_reloadStats.attempted;
			try {
				stl::write_vfunc<Subclass, 0, ReloadHook<Tag>>();
				++g_reloadStats.succeeded;
				L->info("Patched ReloadShaders for {}", Tag::Name());
			} catch (const std::exception& ex) {
				++g_reloadStats.failed;
				L->warn("Failed to patch ReloadShaders for {}: {}", Tag::Name(), ex.what());
			} catch (...) {
				++g_reloadStats.failed;
				L->warn("Failed to patch ReloadShaders for {}: unknown exception", Tag::Name());
			}
		}

		template <class Subclass, class Tag>
		void TryInstallSetupTechnique()
		{
			++g_setupStats.attempted;
			try {
				stl::write_vfunc<Subclass, 0, SetupTechniqueHook<Tag>>();
				++g_setupStats.succeeded;
				L->info("Patched SetupTechnique for {}", Tag::Name());
			} catch (const std::exception& ex) {
				++g_setupStats.failed;
				L->warn("Failed to patch SetupTechnique for {}: {}", Tag::Name(), ex.what());
			} catch (...) {
				++g_setupStats.failed;
				L->warn("Failed to patch SetupTechnique for {}: unknown exception", Tag::Name());
			}
		}

#define CS_CATALOG_HOOK_SUBCLASS(klass)                                      \
	struct Tag_##klass { static const char* Name() { return #klass; } };     \
	TryInstallReload<RE::klass, Tag_##klass>();                              \
	TryInstallSetupTechnique<RE::klass, Tag_##klass>()
	}

	void InstallAll()
	{
		std::call_once(g_once, [] {
			CS_CATALOG_HOOK_SUBCLASS(BSBloodSplatterShader);
			CS_CATALOG_HOOK_SUBCLASS(BSDFCompositeShader);
			CS_CATALOG_HOOK_SUBCLASS(BSDFLightShader);
			CS_CATALOG_HOOK_SUBCLASS(BSDFPrePassShader);
			CS_CATALOG_HOOK_SUBCLASS(BSDistantTreeShader);
			CS_CATALOG_HOOK_SUBCLASS(BSEffectShader);
			CS_CATALOG_HOOK_SUBCLASS(BSFaceCustomizationShader);
			CS_CATALOG_HOOK_SUBCLASS(BSLightingShader);
			CS_CATALOG_HOOK_SUBCLASS(BSParticleShader);
			CS_CATALOG_HOOK_SUBCLASS(BSSkyShader);
			CS_CATALOG_HOOK_SUBCLASS(BSUtilityShader);
			CS_CATALOG_HOOK_SUBCLASS(BSWaterShader);
			L->info("Subclass ReloadShaders hooks: {}/{} patched ({} failed)",
				g_reloadStats.succeeded, g_reloadStats.attempted, g_reloadStats.failed);
			L->info("Subclass SetupTechnique hooks: {}/{} patched ({} failed)",
				g_setupStats.succeeded, g_setupStats.attempted, g_setupStats.failed);
		});
	}

	InstallStats GetInstallStats()
	{
		return GetReloadInstallStats();
	}

	InstallStats GetReloadInstallStats()
	{
		return g_reloadStats;
	}

	InstallStats GetSetupTechniqueInstallStats()
	{
		return g_setupStats;
	}

	RuntimeStats GetRuntimeStats()
	{
		return RuntimeStats{
			g_setupCalls.load(std::memory_order_relaxed),
			g_mapAttributions.load(std::memory_order_relaxed)
		};
	}
}
