#include "Render/ShaderSubclassHooks.h"

#include "Log.h"
#include "PCH.h"
#include "Render/Engine.h"
#include "Render/FrameProfiler.h"
#include "Render/NativeShaderFamily.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassContext.h"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "RE/B/BSBloodSplatterShader.h"
#include "RE/B/BSDFCompositeShader.h"
#include "RE/B/BSDFLightShader.h"
#include "RE/B/BSDFPrePassShader.h"
#include "RE/B/BSDistantTreeShader.h"
#include "RE/B/BSEffectShader.h"
#include "RE/B/BSLightingShader.h"
#include "RE/B/BSParticleShader.h"
#include "RE/B/BSRenderPass.h"
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

		// Fixed before the first frame renders, so the render thread reads it without a lock.
		std::vector<PrepassDrawObserver> g_prepassObservers;

		// The engine bakes most prepass draws into command-buffer records that ProcessCommandBuffer replays
		// without SetupGeometry, so each live record is mapped back to its pass.
		class PrepassRecordRegistry
		{
		public:
			void Add(const std::byte* a_record, RE::BSRenderPass* a_pass)
			{
				std::unique_lock lock(_mutex);
				_passes[a_record] = a_pass;
			}

			void Remove(const std::byte* a_record)
			{
				std::unique_lock lock(_mutex);
				_passes.erase(a_record);
			}

			void Resolve(std::span<std::byte* const> a_records, std::vector<RE::BSRenderPass*>& a_passes) const
			{
				a_passes.clear();
				std::shared_lock lock(_mutex);
				for (const auto* record : a_records) {
					const auto it = _passes.find(record);
					a_passes.push_back(it != _passes.end() ? it->second : nullptr);
				}
			}

		private:
			mutable std::shared_mutex _mutex;
			std::unordered_map<const std::byte*, RE::BSRenderPass*> _passes;
		};
		PrepassRecordRegistry g_prepassRecords;

		struct PrepassSetupGeometryHook
		{
			static constexpr std::size_t size = 0x07;

			static void thunk(
				RE::BSShader* a_self,
				RE::BSRenderPass* a_pass)
			{
				func(a_self, a_pass);
				for (const auto& observer : g_prepassObservers)
					observer.apply(observer.classify(a_pass));
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct PrepassCreateCommandBufferHook
		{
			static std::byte* thunk(
				RE::BSShader* a_self,
				RE::BSRenderPass* a_pass)
			{
				auto* record = func(a_self, a_pass);
				if (record && !g_prepassObservers.empty())
					g_prepassRecords.Add(record, a_pass);
				return record;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct CleanupCommandBufferHook
		{
			static std::int64_t thunk(
				void* a_renderer,
				std::byte* a_record)
			{
				if (!g_prepassObservers.empty())
					g_prepassRecords.Remove(a_record);
				return func(a_renderer, a_record);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The record array is null-terminated; OG reads the third argument, NG/AE ignore it and the fourth.
		struct ProcessCommandBufferHook
		{
			static std::int64_t thunk(
				void* a_renderer,
				std::byte** a_records,
				void* a_arg3,
				void* a_arg4)
			{
				if (g_prepassObservers.empty() || !a_records || !*a_records)
					return func(a_renderer, a_records, a_arg3, a_arg4);
				return ReplayByClass(a_renderer, a_records, a_arg3, a_arg4);
			}

			static inline REL::Relocation<decltype(thunk)> func;

		private:
			// Replays each run of equal-class records in its own engine call so apply lands between draws.
			static std::int64_t ReplayByClass(
				void* a_renderer,
				std::byte** a_records,
				void* a_arg3,
				void* a_arg4)
			{
				thread_local std::vector<RE::BSRenderPass*> passes;
				thread_local std::vector<std::uint32_t> classes;

				std::size_t count = 0;
				while (a_records[count])
					++count;
				g_prepassRecords.Resolve({ a_records, count }, passes);

				const auto width = g_prepassObservers.size();
				classes.assign(count * width, 0u);
				for (std::size_t i = 0; i < count; ++i) {
					if (!passes[i])
						continue;
					for (std::size_t j = 0; j < width; ++j)
						classes[i * width + j] = g_prepassObservers[j].classify(passes[i]);
				}

				const auto row = [&](std::size_t a_index) { return classes.begin() + static_cast<std::ptrdiff_t>(a_index * width); };
				std::int64_t result = 0;
				for (std::size_t start = 0; start < count;) {
					std::size_t end = start + 1;
					while (end < count && std::equal(row(start), row(start) + static_cast<std::ptrdiff_t>(width), row(end)))
						++end;
					for (std::size_t j = 0; j < width; ++j)
						g_prepassObservers[j].apply(classes[start * width + j]);
					std::byte* const next = a_records[end];
					a_records[end] = nullptr;
					result = func(a_renderer, a_records + start, a_arg3, a_arg4);
					a_records[end] = next;
					start = end;
				}
				return result;
			}
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

	bool RegisterPrepassDrawObserver(PrepassDrawObserver a_observer)
	{
		static const bool installed =
			TryPatch(
				"BSDFPrePassShader SetupGeometry observer",
				[] { stl::write_vfunc<RE::BSDFPrePassShader, PrepassSetupGeometryHook>(); }) &&
			TryPatch(
				"BSDFPrePassShader command-buffer builder observer",
				[] { stl::detour_thunk<PrepassCreateCommandBufferHook>(REL::ID({ 1285447, 2318501, 2318501 })); }) &&
			TryPatch(
				"command-buffer cleanup observer",
				[] { stl::detour_thunk<CleanupCommandBufferHook>(REL::ID({ 1544902, 2276988, 2276988 })); }) &&
			TryPatch(
				"command-buffer replay observer",
				[] { stl::detour_thunk<ProcessCommandBufferHook>(REL::ID({ 673619, 2276979, 2276979 })); });
		if (!installed || !a_observer.classify || !a_observer.apply)
			return false;
		g_prepassObservers.push_back(a_observer);
		return true;
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
