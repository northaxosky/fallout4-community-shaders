#include "Render/ShaderSubclassHooks.h"

#include "Log.h"
#include "PCH.h"
#include "Render/ShaderSubclassContext.h"
#include "Render/ShaderVariantRuntimeResolver.h"

#include <Windows.h>

#include <exception>
#include <mutex>
#include <optional>

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

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shadersubclass");

		ShaderSubclassHookInstallStats g_reloadStats{};
		ShaderSubclassHookInstallStats g_setupStats{};
		std::once_flag g_installOnce;

		template <class Tag>
		struct ReloadHook
		{
			static constexpr std::size_t size = 0x0B;

			static void thunk(void* a_self, bool a_clear)
			{
				shader_context::Scope scope(
					a_self, Tag::Name(), std::nullopt);
				func(a_self, a_clear);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class Tag>
		struct SetupTechniqueHook
		{
			static constexpr std::size_t size = 0x02;

			static bool thunk(
				RE::BSShader* a_self,
				std::uint32_t a_techniqueBits)
			{
				shader_context::Scope scope(
					a_self, Tag::Name(), a_techniqueBits);
				return func(a_self, a_techniqueBits);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class Subclass, class Tag>
		void TryInstallReload()
		{
			++g_reloadStats.attempted;
			try {
				stl::write_vfunc<Subclass, 0, ReloadHook<Tag>>();
				++g_reloadStats.succeeded;
				L->info("Patched ReloadShaders for {}", Tag::Name());
			} catch (const std::exception& e) {
				++g_reloadStats.failed;
				L->warn(
					"Failed to patch ReloadShaders for {}: {}",
					Tag::Name(),
					e.what());
			} catch (...) {
				++g_reloadStats.failed;
				L->warn(
					"Failed to patch ReloadShaders for {}: unknown exception",
					Tag::Name());
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
			} catch (const std::exception& e) {
				++g_setupStats.failed;
				L->warn(
					"Failed to patch SetupTechnique for {}: {}",
					Tag::Name(),
					e.what());
			} catch (...) {
				++g_setupStats.failed;
				L->warn(
					"Failed to patch SetupTechnique for {}: unknown exception",
					Tag::Name());
			}
		}

#define CS_HOOK_SHADER_SUBCLASS(klass)                                      \
	struct Tag_##klass { static const char* Name() { return #klass; } };    \
	TryInstallReload<RE::klass, Tag_##klass>();                             \
	TryInstallSetupTechnique<RE::klass, Tag_##klass>()
	}

	void InstallShaderSubclassHooks()
	{
		std::call_once(g_installOnce, [] {
			CS_HOOK_SHADER_SUBCLASS(BSBloodSplatterShader);
			CS_HOOK_SHADER_SUBCLASS(BSDFCompositeShader);
			CS_HOOK_SHADER_SUBCLASS(BSDFLightShader);
			CS_HOOK_SHADER_SUBCLASS(BSDFPrePassShader);
			CS_HOOK_SHADER_SUBCLASS(BSDistantTreeShader);
			CS_HOOK_SHADER_SUBCLASS(BSEffectShader);
			CS_HOOK_SHADER_SUBCLASS(BSFaceCustomizationShader);
			CS_HOOK_SHADER_SUBCLASS(BSLightingShader);
			CS_HOOK_SHADER_SUBCLASS(BSParticleShader);
			CS_HOOK_SHADER_SUBCLASS(BSSkyShader);
			CS_HOOK_SHADER_SUBCLASS(BSUtilityShader);
			CS_HOOK_SHADER_SUBCLASS(BSWaterShader);

			L->info(
				"Subclass ReloadShaders hooks: {}/{} patched ({} failed)",
				g_reloadStats.succeeded,
				g_reloadStats.attempted,
				g_reloadStats.failed);
			L->info(
				"Subclass SetupTechnique hooks: {}/{} patched ({} failed)",
				g_setupStats.succeeded,
				g_setupStats.attempted,
				g_setupStats.failed);
			if (g_setupStats.succeeded == 0) {
				L->error(
					"Shader injection dispatch mode: exact-hash fallback "
					"(no SetupTechnique hooks installed).");
			} else if (!IsPixelShaderVariantResolutionAvailable()) {
				L->info(
					"Shader injection dispatch mode: exact-hash fallback "
					"(resolved variant keys unavailable for this runtime).");
			} else {
				L->info(
					"Shader injection dispatch mode: resolved variant key "
					"with exact-hash fallback.");
			}
		});
	}
}
