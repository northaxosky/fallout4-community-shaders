#include "Render/ShaderSubclassHooks.h"

#include "Log.h"
#include "PCH.h"
#include "Render/Engine.h"
#include "Render/FrameProfiler.h"
#include "Render/NativeShaderFamily.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassContext.h"
#include "Telemetry/Telemetry.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
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
#include "RE/B/BSGeometry.h"
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

		// Restores the previous value so nested engine calls on one thread unwind correctly.
		template <class T>
		class ThreadLocalPointerScope
		{
		public:
			ThreadLocalPointerScope(T*& a_slot, T* a_value) noexcept :
				_slot(a_slot),
				_previous(a_slot)
			{
				_slot = a_value;
			}

			~ThreadLocalPointerScope() noexcept { _slot = _previous; }

			ThreadLocalPointerScope(const ThreadLocalPointerScope&) = delete;
			ThreadLocalPointerScope& operator=(const ThreadLocalPointerScope&) = delete;

		private:
			T*& _slot;
			T* _previous;
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
				const ThreadLocalPointerScope scope(t_activeBeginTechnique, &active);
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

		constexpr std::size_t kPrepassLaneComponents = 4;
		constexpr std::size_t kPrepassBakePaths = 2;

		struct ClassifierCounts
		{
			std::atomic<std::uint64_t> classified{ 0 };
			std::atomic<std::uint64_t> flagged{ 0 };
		};

		// Set at Load before the first frame renders, so engine threads read them without a lock.
		std::array<PrepassDrawClassifier, kPrepassLaneComponents> g_prepassClassifiers{};
		bool g_prepassLaneActive = false;
		std::array<std::array<ClassifierCounts, kPrepassBakePaths>, kPrepassLaneComponents> g_classifierCounts;
		std::atomic<std::uint64_t> g_laneBaked{ 0 };
		std::atomic<std::uint64_t> g_laneUnavailable{ 0 };

		// Stock never reads the cb2 pad declared directly before material flags (constantTable byte 0x60), so it carries the lane.
		constexpr std::size_t kPrepassMaterialFlagsSlot = 0x60 - 0x58;
		constexpr std::uint64_t kLaneDiagnosticLimit = 16;
		// Landscape draws lay that register out as land_material_gate, which stock reads.
		constexpr std::uint32_t kPrepassLandscapeBit = 1U << 5;
		// Context::GetConstantBuffer level of the per-geometry cb2.
		constexpr std::uint32_t kPerGeometryConstantLevel = 2;

		struct PrepassLaneRequest
		{
			PrepassBakePath path;
			std::uint32_t descriptor;
			std::array<float, kPrepassLaneComponents> lane;
			std::int32_t offset;  // dword offset in the pixel cb2 constants; -1 when unresolved
		};

		thread_local PrepassLaneRequest* t_prepassLane = nullptr;

		[[nodiscard]] std::int32_t PrepassLaneOffset(const RE::BSGraphics::PixelShader* a_pixel) noexcept
		{
			if (!a_pixel)
				return -1;
			// Table bytes are dword offsets; 0xFF marks a constant the variant does not have.
			const auto flags = static_cast<std::uint8_t>(a_pixel->constantTable[kPrepassMaterialFlagsSlot]);
			return flags != 0xFF && flags >= 4 ? flags - 4 : -1;
		}

		void CountLane(bool a_baked, std::uint32_t a_descriptor, std::int32_t a_offset) noexcept
		{
			if (a_baked) {
				g_laneBaked.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			if (g_laneUnavailable.fetch_add(1, std::memory_order_relaxed) < kLaneDiagnosticLimit)
				L->warn("Prepass lane unavailable: descriptor=0x{:08X} offset={}", a_descriptor, a_offset);
		}

		[[nodiscard]] const RE::BSGraphics::PixelShader* FindPrepassPixelShader(
			const RE::BSShader* a_shader,
			std::uint32_t a_descriptor) noexcept
		{
			RE::BSGraphics::PixelShader key;
			key.id = RE::BSDFPrePassShader::GetPixelShaderID(a_descriptor);
			auto* keyPointer = &key;
			const auto& shaders = native::PixelShaders(a_shader);
			const auto found = shaders.find(keyPointer);
			return found != shaders.end() ? *found : nullptr;
		}

		// Unregistered components stay 0 so the whole register is deterministic.
		[[nodiscard]] PrepassLaneRequest MakePrepassLaneRequest(
			RE::BSShader* a_shader,
			RE::BSRenderPass* a_pass,
			PrepassBakePath a_path) noexcept
		{
			PrepassLaneRequest request{
				.path = a_path,
				.descriptor = a_pass->passEnum,
				.lane = {},
				.offset = PrepassLaneOffset(FindPrepassPixelShader(a_shader, a_pass->passEnum))
			};
			for (std::size_t component = 0; component < kPrepassLaneComponents; ++component) {
				const auto classifier = g_prepassClassifiers[component];
				if (!classifier)
					continue;
				const bool flagged = classifier(a_pass, a_path);
				auto& counts = g_classifierCounts[component][static_cast<std::size_t>(a_path)];
				counts.classified.fetch_add(1, std::memory_order_relaxed);
				counts.flagged.fetch_add(flagged ? 1u : 0u, std::memory_order_relaxed);
				request.lane[component] = flagged ? 1.0f : 0.0f;
			}
			return request;
		}

		// Immediate draws fill cb2 inside SetupGeometry: write the lane at the map on NG/AE, at the pre-unmap flush on OG (each inlines the other).
		struct PrepassSetupGeometryHook
		{
			static constexpr std::size_t size = 0x07;

			static void thunk(
				RE::BSShader* a_self,
				RE::BSRenderPass* a_pass)
			{
				if (!g_prepassLaneActive || !a_pass || (a_pass->passEnum & kPrepassLandscapeBit) != 0) {
					func(a_self, a_pass);
					return;
				}
				auto request = MakePrepassLaneRequest(a_self, a_pass, PrepassBakePath::kImmediate);
				const ThreadLocalPointerScope scope(t_prepassLane, &request);
				func(a_self, a_pass);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		void WriteImmediateLane(const RE::BSGraphics::ConstantGroup* a_group) noexcept
		{
			const auto* request = t_prepassLane;
			if (!request || request->path != PrepassBakePath::kImmediate)
				return;
			const bool fits = request->offset >= 0 && a_group && a_group->data;
			if (fits)
				std::ranges::copy(request->lane, a_group->data + request->offset);
			CountLane(fits, request->descriptor, request->offset);
		}

		struct PixelConstantMapHook
		{
			static RE::BSGraphics::ConstantGroup* thunk(
				RE::BSGraphics::Renderer* a_renderer,
				RE::BSGraphics::PixelShader* a_pixel,
				std::uint32_t a_level)
			{
				auto* group = func(a_renderer, a_pixel, a_level);
				if (a_level == kPerGeometryConstantLevel)
					WriteImmediateLane(group);
				return group;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// OG unmaps both level-2 groups here after SetupGeometry fills them; that is its only caller inside the lane scope.
		struct ConstantGroupFlushHook
		{
			static void thunk(
				RE::BSGraphics::Renderer* a_renderer,
				RE::BSGraphics::ConstantGroup* a_vertex,
				RE::BSGraphics::ConstantGroup* a_pixel)
			{
				WriteImmediateLane(a_pixel);
				func(a_renderer, a_vertex, a_pixel);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// cb2 is baked once into an immutable record, so the lane rides the constants BuildCommandBuffer copies; the scope excludes utility shaders.
		struct PrepassCreateCommandBufferHook
		{
			static std::byte* thunk(
				RE::BSShader* a_self,
				RE::BSRenderPass* a_pass)
			{
				if (!g_prepassLaneActive || !a_pass || (a_pass->passEnum & kPrepassLandscapeBit) != 0)
					return func(a_self, a_pass);
				auto request = MakePrepassLaneRequest(a_self, a_pass, PrepassBakePath::kCommandBuffer);
				const ThreadLocalPointerScope scope(t_prepassLane, &request);
				return func(a_self, a_pass);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BuildCommandBufferHook
		{
			static std::byte* thunk(
				RE::BSShader* a_self,
				RE::BSShader::BuildCommandBufferParam& a_param)
			{
				if (const auto* request = t_prepassLane; request && request->path == PrepassBakePath::kCommandBuffer) {
					const bool fits = request->offset >= 0 && a_param.pixelConstants &&
					                  static_cast<std::uint32_t>(request->offset) + kPrepassLaneComponents <= a_param.pixelRegisters * 4;
					if (fits)
						std::ranges::copy(request->lane, a_param.pixelConstants + request->offset);
					CountLane(fits, request->descriptor, request->offset);
				}
				return func(a_self, a_param);
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

	bool RegisterPrepassDrawClassifier(PrepassLaneComponent a_component, PrepassDrawClassifier a_classifier)
	{
		static const bool installed =
			TryPatch(
				"BSDFPrePassShader SetupGeometry lane",
				[] { stl::write_vfunc<RE::BSDFPrePassShader, PrepassSetupGeometryHook>(); }) &&
			TryPatch(
				"immediate pixel constants lane",
				[] {
					if (REX::FModule::IsRuntimeOG())
						stl::detour_thunk<ConstantGroupFlushHook>(RE::ID::BSGraphics::Renderer::FlushConstantGroup);
					else
						stl::detour_thunk<PixelConstantMapHook>(RE::ID::BSGraphics::Renderer::GetShaderConstantGroupPS);
				}) &&
			TryPatch(
				"BSDFPrePassShader command-buffer creator lane",
				[] { stl::detour_thunk<PrepassCreateCommandBufferHook>(RE::ID::BSDFPrePassShader::CreateCommandBuffer); }) &&
			TryPatch(
				"command-buffer builder lane",
				[] { stl::detour_thunk<BuildCommandBufferHook>(RE::ID::BSShader::BuildCommandBuffer); });
		auto& slot = g_prepassClassifiers[static_cast<std::size_t>(a_component)];
		if (!installed || !a_classifier || slot)
			return false;
		slot = a_classifier;
		g_prepassLaneActive = true;
		return true;
	}

	PrepassLaneStats GetPrepassLaneStats() noexcept
	{
		return { g_laneBaked.load(std::memory_order_relaxed), g_laneUnavailable.load(std::memory_order_relaxed) };
	}

	PrepassClassifierStats GetPrepassClassifierStats(PrepassLaneComponent a_component, PrepassBakePath a_path) noexcept
	{
		const auto& counts = g_classifierCounts[static_cast<std::size_t>(a_component)][static_cast<std::size_t>(a_path)];
		return { counts.classified.load(std::memory_order_relaxed), counts.flagged.load(std::memory_order_relaxed) };
	}

	void WritePrepassLaneTelemetry(telemetry::Sink& a_sink, PrepassLaneComponent a_component, std::string_view a_flagged)
	{
		const auto records = GetPrepassClassifierStats(a_component, PrepassBakePath::kCommandBuffer);
		const auto immediate = GetPrepassClassifierStats(a_component, PrepassBakePath::kImmediate);
		const auto lanes = GetPrepassLaneStats();
		a_sink
			.Field("records", records.classified)
			.Field(std::format("{}_records", a_flagged), records.flagged)
			.Field("immediate_draws", immediate.classified)
			.Field(std::format("{}_immediate_draws", a_flagged), immediate.flagged)
			.Field("lanes_baked", lanes.baked)
			.Field("lanes_unavailable", lanes.unavailable);
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
