#include "SubclassHooks.h"

#include "PCH.h"

#include "Log.h"
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

		InstallStats g_stats{};
		std::once_flag g_once;

		template <class Subclass, class Tag>
		void TryInstall()
		{
			++g_stats.attempted;
			try {
				stl::write_vfunc<Subclass, 0, ReloadHook<Tag>>();
				++g_stats.succeeded;
				L->info("Patched ReloadShaders for {}", Tag::Name());
			} catch (const std::exception& ex) {
				++g_stats.failed;
				L->warn("Failed to patch ReloadShaders for {}: {}", Tag::Name(), ex.what());
			} catch (...) {
				++g_stats.failed;
				L->warn("Failed to patch ReloadShaders for {}: unknown exception", Tag::Name());
			}
		}

#define CS_CATALOG_HOOK_SUBCLASS(klass)                                        \
	struct Tag_##klass { static const char* Name() { return #klass; } };       \
	TryInstall<RE::klass, Tag_##klass>()
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
				g_stats.succeeded, g_stats.attempted, g_stats.failed);
		});
	}

	InstallStats GetInstallStats()
	{
		return g_stats;
	}
}
