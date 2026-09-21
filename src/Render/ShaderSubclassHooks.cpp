#include "Render/ShaderSubclassHooks.h"

#include "Log.h"
#include "PCH.h"
#include "Render/Engine.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassContext.h"

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

		ShaderSubclassHookInstallStats g_setupStats{};
		std::once_flag g_installOnce;

		struct ActiveBeginTechnique
		{
			RE::BSShader* shader = nullptr;
			std::uint32_t vertexShaderId = 0;
			std::uint32_t pixelShaderId = 0;
		};

		thread_local const ActiveBeginTechnique* t_activeBeginTechnique =
			nullptr;

		class ActiveBeginTechniqueScope
		{
		public:
			explicit ActiveBeginTechniqueScope(
				const ActiveBeginTechnique& a_active) noexcept :
				_previous(t_activeBeginTechnique)
			{
				t_activeBeginTechnique = &a_active;
			}

			~ActiveBeginTechniqueScope() noexcept
			{
				t_activeBeginTechnique = _previous;
			}

		private:
			const ActiveBeginTechnique* _previous;
		};

		struct BeginTechniqueHook
		{
			static bool thunk(
				RE::BSShader* a_shader,
				std::uint32_t a_vertexShaderId,
				std::uint32_t a_hullShaderId,
				std::uint32_t a_domainShaderId,
				std::uint32_t a_pixelShaderId)
			{
				const ActiveBeginTechnique active{
					.shader = a_shader,
					.vertexShaderId = a_vertexShaderId,
					.pixelShaderId = a_pixelShaderId
				};
				const ActiveBeginTechniqueScope scope(active);
				return func(
					a_shader,
					a_vertexShaderId,
					a_hullShaderId,
					a_domainShaderId,
					a_pixelShaderId);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct SetShadersHook
		{
			static void thunk(
				void* a_renderer,
				RE::BSGraphics::VertexShader* a_vertex,
				RE::BSGraphics::HullShader* a_hull,
				RE::BSGraphics::DomainShader* a_domain,
				RE::BSGraphics::PixelShader* a_pixel)
			{
				if (!t_activeBeginTechnique) {
					func(
						a_renderer,
						a_vertex,
						a_hull,
						a_domain,
						a_pixel);
					return;
				}
				const auto replacement =
					ResolveNativeGraphicsShaderBinding(
						t_activeBeginTechnique->shader,
						t_activeBeginTechnique->vertexShaderId,
						t_activeBeginTechnique->pixelShaderId,
						a_vertex,
						a_pixel);
				func(
					a_renderer,
					replacement.vertex,
					a_hull,
					a_domain,
					replacement.pixel);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct RunComputeShaderHook
		{
			static void thunk(
				void* a_renderer,
				RE::BSGraphics::ComputeShader* a_compute,
				std::uint32_t a_threadGroupCountX,
				std::uint32_t a_threadGroupCountY,
				std::uint32_t a_threadGroupCountZ)
			{
				func(
					a_renderer,
					ResolveNativeComputeShaderBinding(a_compute),
					a_threadGroupCountX,
					a_threadGroupCountY,
					a_threadGroupCountZ);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ReloadStandaloneComputeHook
		{
			static std::uint32_t thunk(
				void* a_owner,
				RE::BSIStream* a_stream)
			{
				const auto result = func(a_owner, a_stream);
				const std::string_view name =
					native::StandaloneComputeOwnerName(a_owner) ?
						native::StandaloneComputeOwnerName(a_owner) : "";
				if (name == "DFTiledLighting") {
					ObserveNativeComputeOwner(
						a_owner,
						ShaderInjectionTarget::kDfTiledLighting,
						name);
				} else if (name == "IndexBufferOffsetCS") {
					ObserveNativeComputeOwner(
						a_owner,
						ShaderInjectionTarget::kImageSpace,
						name);
				}
				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ReloadFromStreamHook
		{
			static std::uint32_t thunk(
				RE::BSShader* a_shader,
				RE::BSIStream* a_stream)
			{
				const auto result = func(a_shader, a_stream);
				ObserveNativeShader(a_shader, a_stream);
				return result;
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

#define CS_HOOK_SHADER_SUBCLASS(klass, target)                              \
	struct Tag_##klass {                                                    \
		static const char* Name() { return #klass; }                         \
	};                                                                      \
	TryInstallSetupTechnique<RE::klass, Tag_##klass>()
	}

	void InstallShaderSubclassHooks()
	{
		std::call_once(g_installOnce, [] {
			if (!REX::FModule::IsRuntimeAE()) {
				L->warn(
					"Native shader subclass hooks require Fallout 4 AE 1.11.240; "
					"baseline shader ownership remains inactive.");
				return;
			}
			CS_HOOK_SHADER_SUBCLASS(BSBloodSplatterShader, kBloodSplatter);
			CS_HOOK_SHADER_SUBCLASS(BSDFCompositeShader, kBsdfComposite);
			CS_HOOK_SHADER_SUBCLASS(BSDFLightShader, kBsdfLight);
			CS_HOOK_SHADER_SUBCLASS(BSDFPrePassShader, kDeferredPrepass);
			CS_HOOK_SHADER_SUBCLASS(BSDistantTreeShader, kDistantTree);
			CS_HOOK_SHADER_SUBCLASS(BSEffectShader, kEffect);
			CS_HOOK_SHADER_SUBCLASS(BSFaceCustomizationShader, kFaceCustomization);
			CS_HOOK_SHADER_SUBCLASS(BSLightingShader, kBsLighting);
			CS_HOOK_SHADER_SUBCLASS(BSParticleShader, kParticle);
			CS_HOOK_SHADER_SUBCLASS(BSSkyShader, kBsSky);
			CS_HOOK_SHADER_SUBCLASS(BSUtilityShader, kUtility);
			CS_HOOK_SHADER_SUBCLASS(BSWaterShader, kBsWater);
			try {
				stl::detour_thunk<ReloadFromStreamHook>(
					REL::ID(2318873));
				L->info("Patched BSShader archive loader observer");
			} catch (const std::exception& e) {
				L->error(
					"Failed to patch BSShader archive loader observer: {}",
					e.what());
			} catch (...) {
				L->error(
					"Failed to patch BSShader archive loader observer.");
			}
			try {
				stl::detour_thunk<SetShadersHook>(
					REL::ID(2276942));
				L->info("Patched native graphics shader-set boundary");
			} catch (const std::exception& e) {
				L->error(
					"Failed to patch native graphics shader-set boundary: {}",
					e.what());
			} catch (...) {
				L->error(
					"Failed to patch native graphics shader-set boundary.");
			}
			try {
				stl::detour_thunk<RunComputeShaderHook>(
					REL::ID(2276940));
				L->info("Patched native compute shader-run boundary");
			} catch (const std::exception& e) {
				L->error(
					"Failed to patch native compute shader-run boundary: {}",
					e.what());
			} catch (...) {
				L->error(
					"Failed to patch native compute shader-run boundary.");
			}
			try {
				stl::detour_thunk<ReloadStandaloneComputeHook>(
					REL::ID(2319682));
				L->info("Patched standalone compute shader observers");
			} catch (const std::exception& e) {
				L->error(
					"Failed to patch standalone compute shader observers: {}",
					e.what());
			} catch (...) {
				L->error(
					"Failed to patch standalone compute shader observers.");
			}
			++g_setupStats.attempted;
			try {
				stl::detour_thunk<BeginTechniqueHook>(
					REL::ID(2318876));
				++g_setupStats.succeeded;
				L->info("Patched BSShader native technique binder");
			} catch (const std::exception& e) {
				++g_setupStats.failed;
				L->error(
					"Failed to patch BSShader native technique binder: {}",
					e.what());
			} catch (...) {
				++g_setupStats.failed;
				L->error(
					"Failed to patch BSShader native technique binder.");
			}

			L->info(
				"Subclass SetupTechnique hooks: {}/{} patched ({} failed)",
				g_setupStats.succeeded,
				g_setupStats.attempted,
				g_setupStats.failed);
			if (g_setupStats.succeeded == 0)
				L->error("Native shader descriptor routing unavailable.");
			else
				L->info(
					"Shader injection dispatch mode: native family/stage descriptor.");
		});
	}
}
