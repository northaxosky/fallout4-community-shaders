#include "Render/ShaderSubclassHooks.h"

#include "Log.h"
#include "PCH.h"
#include "Render/Engine.h"
#include "Render/FrameProfiler.h"
#include "Render/NativeShaderFamily.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassContext.h"

#include <Windows.h>

#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>

#include "RE/B/BSBloodSplatterShader.h"
#include "RE/B/BSDFCompositeShader.h"
#include "RE/B/BSDFLightShader.h"
#include "RE/B/BSDFPrePassShader.h"
#include "RE/B/BSDistantTreeShader.h"
#include "RE/B/BSEffectShader.h"
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
				const bool result = func(
					a_shader,
					a_vertexShaderId,
					a_hullShaderId,
					a_domainShaderId,
					a_pixelShaderId);
				// FO4: family IDs come from the native binder, not Skyrim enum indices.
				if (result) {
					const auto* name = native::FxpFilename(a_shader);
					render::profiling::SetShaderFamily(native::ShaderType(a_shader), name ? name : "Unknown");
				}
				return result;
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
				const bool hasPayload =
					native::ShaderArchiveStreamHasPayload(a_stream);
				const auto result = func(a_owner, a_stream);
				const std::string_view name =
					native::StandaloneComputeOwnerName(a_owner) ?
						native::StandaloneComputeOwnerName(a_owner) :
						"";
				if (const auto target = ResolveStandaloneComputeTarget(name)) {
					ObserveNativeComputeOwner(
						a_owner,
						*target,
						name,
						hasPayload && *target == ShaderInjectionTarget::kDfTiledLighting);
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
				const bool hasPayload =
					native::ShaderArchiveStreamHasPayload(a_stream);
				const auto result = func(a_shader, a_stream);
				ObserveNativeShader(a_shader, hasPayload);
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

		template <class Install>
		bool TryPatch(std::string_view a_label, Install&& a_install)
		{
			try {
				a_install();
				L->info("Patched {}", a_label);
				return true;
			} catch (const std::exception& e) {
				L->error("Failed to patch {}: {}", a_label, e.what());
			} catch (...) {
				L->error("Failed to patch {}.", a_label);
			}
			return false;
		}

		void CountRoutingHook(bool a_patched) noexcept
		{
			++g_setupStats.attempted;
			++(a_patched ? g_setupStats.succeeded : g_setupStats.failed);
		}

		template <class Subclass, class Tag>
		void TryInstallSetupTechnique()
		{
			CountRoutingHook(TryPatch(
				std::format("SetupTechnique for {}", Tag::Name()),
				[] { stl::write_vfunc<Subclass, 0, SetupTechniqueHook<Tag>>(); }));
		}

#define CS_HOOK_SHADER_SUBCLASS(klass, target)       \
	struct Tag_##klass                               \
	{                                                \
		static const char* Name() { return #klass; } \
	};                                               \
	TryInstallSetupTechnique<RE::klass, Tag_##klass>()
	}

	void InstallShaderSubclassHooks()
	{
		std::call_once(g_installOnce, [] {
			CS_HOOK_SHADER_SUBCLASS(BSBloodSplatterShader, kBloodSplatter);
			CS_HOOK_SHADER_SUBCLASS(BSDFCompositeShader, kBsdfComposite);
			CS_HOOK_SHADER_SUBCLASS(BSDFLightShader, kBsdfLight);
			CS_HOOK_SHADER_SUBCLASS(BSDFPrePassShader, kDeferredPrepass);
			CS_HOOK_SHADER_SUBCLASS(BSDistantTreeShader, kDistantTree);
			CS_HOOK_SHADER_SUBCLASS(BSEffectShader, kEffect);
			CS_HOOK_SHADER_SUBCLASS(BSLightingShader, kBsLighting);
			CS_HOOK_SHADER_SUBCLASS(BSParticleShader, kParticle);
			CS_HOOK_SHADER_SUBCLASS(BSSkyShader, kBsSky);
			CS_HOOK_SHADER_SUBCLASS(BSUtilityShader, kUtility);
			CS_HOOK_SHADER_SUBCLASS(BSWaterShader, kBsWater);

			TryPatch("BSShader archive loader observer", [] {
				stl::detour_thunk<ReloadFromStreamHook>(REL::ID({ 101507, 2318873, 2318873 }));
			});
			TryPatch("native graphics shader-set boundary", [] {
				stl::detour_thunk<SetShadersHook>(REL::ID({ 894905, 2276942, 2276942 }));
			});
			TryPatch("native compute shader-run boundary", [] {
				stl::detour_thunk<RunComputeShaderHook>(REL::ID({ 1108829, 2276940, 2276940 }));
			});
			TryPatch("standalone compute shader observers", [] {
				stl::detour_thunk<ReloadStandaloneComputeHook>(REL::ID({ 166975, 2319682, 2319682 }));
			});
			CountRoutingHook(TryPatch("BSShader native technique binder", [] {
				stl::detour_thunk<BeginTechniqueHook>(REL::ID({ 1041640, 2318876, 2318876 }));
			}));

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
