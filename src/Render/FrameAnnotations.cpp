#include "Render/FrameAnnotations.h"

#include "Log.h"
#include "PCH.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/HookRegistry.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderGeometryHooks.h"
#include "Render/ShaderSubclassHooks.h"
#include "Settings/FeatureConfig.h"
#include "Settings/SettingsRegistry.h"

#include "RE/B/BSBloodSplatterShader.h"
#include "RE/B/BSCubeMapCamera.h"
#include "RE/B/BSDFCompositeShader.h"
#include "RE/B/BSDFLightShader.h"
#include "RE/B/BSDFPrePassShader.h"
#include "RE/B/BSDistantTreeShader.h"
#include "RE/B/BSEffectShader.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSImagespaceShader.h"
#include "RE/B/BSLightingShader.h"
#include "RE/B/BSParticleShader.h"
#include "RE/B/BSRenderPass.h"
#include "RE/B/BSShaderAccumulator.h"
#include "RE/B/BSSkyShader.h"
#include "RE/B/BSUtilityShader.h"
#include "RE/B/BSWaterShader.h"

#include <atomic>
#include <concepts>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace cs::render::frame_annotations
{
	namespace
	{
		auto* L = cs::log::Get("cs.annotations");

		bool g_installed = false;
		std::atomic_bool g_requested{ false };
		std::atomic_bool g_live{ false };
		std::atomic_int g_forces{ 0 };

		enum class Gate : std::uint8_t
		{
			kAlways,
			kLive
		};

		// Per-draw names cost CPU, so build them only for a consumer.
		[[nodiscard]] bool AnnotateDraws() noexcept
		{
			return Enabled() && annotation::EventsConsumed();
		}

		[[nodiscard]] bool Open(Gate a_gate) noexcept
		{
			return a_gate == Gate::kAlways || AnnotateDraws();
		}

		// The name is a string_view or a generator, run only when the gate is open.
		template <class Name>
		[[nodiscard]] std::string_view NameView(const Name& a_name, std::string& a_storage)
		{
			if constexpr (std::invocable<const Name&>) {
				a_storage = a_name();
				return a_storage;
			} else {
				return a_name;
			}
		}

		// Annotation only: the profiler times leaf passes, not containers.
		class Scope
		{
		public:
			template <class Name>
			Scope(Gate a_gate, const Name& a_name)
			{
				if (!Open(a_gate))
					return;
				std::string storage;
				_event.emplace(NameView(a_name, storage), false);
			}

			Scope(const Scope&) = delete;
			Scope& operator=(const Scope&) = delete;

		private:
			std::optional<annotation::ScopedEvent> _event;
		};

		[[nodiscard]] std::string_view ShaderLabel(const RE::BSShader* a_shader) noexcept
		{
			const char* name = a_shader ? a_shader->GetFxpFilename() : nullptr;
			return name ? name : "Unknown";
		}

		// SetupGeometry has no pixel shader id; BeginTechnique saw it last.
		struct BoundTechnique
		{
			const RE::BSShader* shader{};
			std::uint32_t pixelShaderId{};
		};
		thread_local BoundTechnique t_bound;
		thread_local annotation::SpannedEvent t_geometry;

		void AnnotateSetupGeometry(RE::BSShader* a_self, RE::BSRenderPass* a_pass) noexcept
		{
			if (!AnnotateDraws() || !a_pass)
				return;
			try {
				t_geometry.Begin(
					std::format("[{}:{:X}] ({:X}) {}",
						ShaderLabel(a_self),
						t_bound.shader == a_self ? t_bound.pixelShaderId : 0,
						a_pass->passEnum,
						a_pass->geometry ? a_pass->geometry->name.c_str() : ""),
					false);
			} catch (...) {
			}
		}

		void AnnotateRestoreGeometry(RE::BSShader*, RE::BSRenderPass*) noexcept
		{
			t_geometry.End();
		}

		// Shared by every imagespace shader; `this` is the embedded effect base.
		[[nodiscard]] std::string ImageSpaceEventName(RE::ImageSpaceEffect* a_effect, std::string_view a_kind)
		{
			auto* shader = reinterpret_cast<RE::BSImagespaceShader*>(
				reinterpret_cast<std::byte*>(a_effect) - RE::BSShader::GetRuntimeSize());
			const char* className = shader->GetShaderClassName();
			return std::format("{} {}", className ? std::string_view{ className } : ShaderLabel(shader), a_kind);
		}

		struct ImageSpaceRender_Hook
		{
			static std::uint64_t thunk(RE::ImageSpaceEffect* a_this, RE::BSTriShape* a_shape, RE::ImageSpaceEffectParam* a_param)
			{
				const Scope scope(Gate::kAlways, [&] { return ImageSpaceEventName(a_this, "Draw"); });
				return func(a_this, a_shape, a_param);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ImageSpaceDispatch_Hook
		{
			static std::uint64_t thunk(
				RE::ImageSpaceEffect* a_this,
				RE::ImageSpaceEffectParam* a_param,
				bool a_useAsyncPipe,
				std::uint32_t a_effect,
				RE::ImageSpaceEffect::EffectDesc* a_desc)
			{
				const Scope scope(Gate::kAlways, [&] { return ImageSpaceEventName(a_this, "Dispatch"); });
				return func(a_this, a_param, a_useAsyncPipe, a_effect, a_desc);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BatchRenderer_RenderBatches_Hook
		{
			static std::uint64_t thunk(RE::BSBatchRenderer* a_this, std::uint32_t a_pass, bool a_alpha, std::uint32_t a_group)
			{
				const Scope scope(Gate::kLive, [&] {
					return std::format("BSBatchRenderer::RenderBatches ({:X})[{}] <{}>", a_pass, a_group, static_cast<unsigned>(a_alpha));
				});
				return func(a_this, a_pass, a_alpha, a_group);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Accumulator_RenderBatches_Hook
		{
			static std::uint64_t thunk(RE::BSShaderAccumulator* a_this, std::uint32_t a_pass, bool a_alpha, std::int32_t a_group)
			{
				const Scope scope(Gate::kLive, [&] {
					return std::format("BSShaderAccumulator::RenderBatches ({:X})[{}] <{}>", a_pass, a_group, static_cast<unsigned>(a_alpha));
				});
				return func(a_this, a_pass, a_alpha, a_group);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct RenderPassImpl_Hook
		{
			static bool thunk(RE::BSBatchRenderer* a_this, RE::BSRenderPass* a_head, std::uint32_t a_technique, bool a_alpha)
			{
				const Scope scope(Gate::kLive, [&] {
					return std::format("BSBatchRenderer::RenderPass [{}:{:X}] <{}>",
						ShaderLabel(a_head ? a_head->shader : nullptr), a_technique, static_cast<unsigned>(a_alpha));
				});
				return func(a_this, a_head, a_technique, a_alpha);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		enum class FinishPhase : std::uint8_t
		{
			kAll,
			kPreResolveDepth,
			kPostResolveDepth
		};

		template <FinishPhase Phase>
		struct FinishAccumulating_Hook
		{
			static constexpr std::string_view kLabels[]{
				"FinishAccumulating", "FinishAccumulatingPreResolveDepth", "FinishAccumulatingPostResolveDepth"
			};

			static void thunk(RE::BSShaderAccumulator* a_this)
			{
				const Scope scope(Gate::kLive, [&] {
					return std::format("BSShaderAccumulator::{} [{}]",
						kLabels[static_cast<std::size_t>(Phase)], static_cast<std::uint32_t>(a_this->renderMode));
				});
				func(a_this);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ClickCubeMap_Hook
		{
			static std::uint64_t thunk(RE::NiAVObject* a_camera, std::int32_t a_faces, bool a_silhouette, bool a_release, bool a_lodOnly)
			{
				const Scope scope(Gate::kAlways, [&] { return std::format("Cubemap {}", a_camera->name.c_str()); });
				return func(a_camera, a_faces, a_silhouette, a_release, a_lodOnly);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Forward_Hook
		{
			static void thunk()
			{
				const Scope scope(Gate::kAlways, "Forward");
				func();
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The engine calls this Render_UI; it is the whole effect chain.
		struct Imagespace_Hook
		{
			static void thunk()
			{
				const Scope scope(Gate::kAlways, "Imagespace");
				func();
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Stock callers pass the cameras plus four floats; the floats are opaque.
		struct DoZPrePass_Hook
		{
			static void thunk(
				RE::BSGraphics::Renderer* a_this,
				RE::NiCamera* a_world,
				RE::NiCamera* a_firstPerson,
				float a_1,
				float a_2,
				float a_3,
				float a_4)
			{
				const Scope scope(Gate::kAlways, "Depth");
				func(a_this, a_world, a_firstPerson, a_1, a_2, a_3, a_4);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class Name>
		void BracketPass(
			bool (*a_pre)(engine::RenderHookCallback, engine::HookPriority),
			bool (*a_post)(engine::RenderHookCallback, engine::HookPriority),
			Name a_name, Gate a_gate = Gate::kAlways)
		{
			auto span = std::make_shared<annotation::SpannedEvent>();
			// Post first: a pre without its post would leave the event open.
			if (!a_post([span] { span->End(); }, engine::HookPriority::OutermostPost) ||
				!a_pre(
					[span, a_name, a_gate] {
						if (!Open(a_gate))
							return;
						std::string storage;
						span->Begin(NameView(a_name, storage), false);
					},
					engine::HookPriority::OutermostPre)) {
				std::string storage;
				L->error("Engine pass '{}' unavailable", NameView(a_name, storage));
			}
		}

		template <engine::ShadowLightKind Kind>
		bool PreShadow(engine::RenderHookCallback a_callback, engine::HookPriority a_priority)
		{
			return engine::RegisterPreShadowLightRender(Kind, std::move(a_callback), a_priority);
		}

		template <engine::ShadowLightKind Kind>
		bool PostShadow(engine::RenderHookCallback a_callback, engine::HookPriority a_priority)
		{
			return engine::RegisterPostShadowLightRender(Kind, std::move(a_callback), a_priority);
		}

		void InstallEnginePasses()
		{
			using namespace engine;
			BracketPass(&RegisterPreDeferredPrePass, &RegisterPostDeferredPrePass, "Deferred Prepass");
			BracketPass(&RegisterPreDeferredLightsImpl, &RegisterPostDeferredLightsImpl, "Deferred Lights");
			BracketPass(&RegisterPrePrecipitationOcclusion, &RegisterPostPrecipitationOcclusion, "Precipitation Occlusion");
			BracketPass(&RegisterPreWaterUpdate, &RegisterPostWaterUpdate, "Water Effects");
			// Replays run in RenderBatches, outside RenderPassImpl ranges.
			BracketPass(&RegisterPreCommandBufferReplay, &RegisterPostCommandBufferReplay, [] { return std::format("BSBatchRenderer::CommandBuffer [{}:{:X}]", ShaderLabel(t_bound.shader), t_bound.pixelShaderId); }, Gate::kLive);
			BracketPass(&PreShadow<ShadowLightKind::kDirectional>, &PostShadow<ShadowLightKind::kDirectional>, "Directional Light Shadowmaps");
			BracketPass(&PreShadow<ShadowLightKind::kFrustum>, &PostShadow<ShadowLightKind::kFrustum>, "Spot Light Shadowmaps");
			BracketPass(&PreShadow<ShadowLightKind::kParabolic>, &PostShadow<ShadowLightKind::kParabolic>, "Omnidirectional Light Shadowmaps");
		}

		template <class Hook>
		void Detour(REL::VariantID a_id, const char* a_name)
		{
			static bool installed = false;
			engine::EnsureDetourInstalled<Hook>(installed, a_id, a_name);
		}

		template <std::size_t Slot, class Hook>
		void Vfunc(const auto& a_vtable)
		{
			stl::write_vfunc<Slot, Hook>(a_vtable[0]);
		}

		void InstallEngineHooks()
		{
			Detour<ImageSpaceRender_Hook>(RE::ID::BSImagespaceShader::Render, "BSImagespaceShader::Render");
			Detour<ImageSpaceDispatch_Hook>(RE::ID::BSImagespaceShader::Dispatch, "BSImagespaceShader::Dispatch");
			Detour<BatchRenderer_RenderBatches_Hook>(RE::ID::BSBatchRenderer::RenderBatches, "BSBatchRenderer::RenderBatches");
			Detour<RenderPassImpl_Hook>(RE::ID::BSBatchRenderer::RenderPassImpl, "BSBatchRenderer::RenderPassImpl");
			Detour<Accumulator_RenderBatches_Hook>(RE::ID::BSShaderAccumulator::RenderBatches, "BSShaderAccumulator::RenderBatches");
			Detour<ClickCubeMap_Hook>(RE::ID::BSCubeMapCamera::ClickCubeMap, "BSCubeMapCamera::ClickCubeMap");
			Detour<DoZPrePass_Hook>(RE::ID::BSGraphics::Renderer::DoZPrePass, "BSGraphics::Renderer::DoZPrePass");
			Detour<Forward_Hook>(RE::ID::DrawWorld::Forward, "DrawWorld::Forward");
			Detour<Imagespace_Hook>(RE::ID::DrawWorld::Imagespace, "DrawWorld::Imagespace");

			Vfunc<0x29, FinishAccumulating_Hook<FinishPhase::kAll>>(RE::VTABLE::BSShaderAccumulator);
			Vfunc<0x2E, FinishAccumulating_Hook<FinishPhase::kPreResolveDepth>>(RE::VTABLE::BSShaderAccumulator);
			Vfunc<0x2F, FinishAccumulating_Hook<FinishPhase::kPostResolveDepth>>(RE::VTABLE::BSShaderAccumulator);

#define CS_HOOK_GEOMETRY(klass, target) engine::ObserveShaderGeometry(engine::ShaderInjectionTarget::target, { &AnnotateSetupGeometry, &AnnotateRestoreGeometry })
			CS_SHADER_SUBCLASSES(CS_HOOK_GEOMETRY);
#undef CS_HOOK_GEOMETRY
		}

		template <class Id>
		void NameTargets(std::string_view a_prefix, auto&& a_resolve)
		{
			for (const auto id : magic_enum::enum_values<Id>()) {
				const auto value = static_cast<std::int32_t>(id);
				if (value <= 0 || id == Id::kCount)
					continue;
				if (auto* texture = a_resolve(id))
					annotation::SetName(texture, std::format("{}/{}", a_prefix, magic_enum::enum_name(id)));
			}
		}
	}

	bool Installed() noexcept
	{
		return g_installed;
	}

	bool Requested() noexcept
	{
		return g_requested.load(std::memory_order_relaxed);
	}

	bool Enabled() noexcept
	{
		return g_live.load(std::memory_order_relaxed) || g_forces.load(std::memory_order_relaxed) > 0;
	}

	bool SetRequested(bool a_requested)
	{
		const auto saved = feature_config::UpdateTopLevelSection(
			"diagnostics", toml::table{ { "frame_annotations", a_requested } },
			feature_config::SectionWrite::kMerge);
		if (!saved) {
			L->warn("Failed to save diagnostics.frame_annotations: {}", saved.error);
			return false;
		}
		g_requested.store(a_requested, std::memory_order_relaxed);
		if (g_installed)
			g_live.store(a_requested, std::memory_order_relaxed);
		return true;
	}

	ScopedForce::ScopedForce() noexcept
	{
		g_forces.fetch_add(1, std::memory_order_relaxed);
	}

	ScopedForce::~ScopedForce() noexcept
	{
		g_forces.fetch_sub(1, std::memory_order_relaxed);
	}

	void Install()
	{
		std::string error;
		const bool requested = settings::ParseCoreSection(
			settings::core::kDiagnostics, feature_config::GetRoot(), "diagnostics", error)
		                           .frameAnnotations;
		if (!error.empty())
			L->warn("{}; using the default", error);
		g_requested.store(requested, std::memory_order_relaxed);
		if (!requested)
			return;
		InstallEnginePasses();
		InstallEngineHooks();
		g_installed = true;
		g_live.store(true, std::memory_order_relaxed);
		L->info("Frame annotations installed");
	}

	void OnDataLoaded()
	{
		if (!g_installed)
			return;
		using RE::BSGraphics::DepthStencilTargetID;
		using RE::BSGraphics::RenderTargetID;
		NameTargets<RenderTargetID>("RenderTarget", [](RenderTargetID a_id) { return engine::GetRenderTargetTexture(a_id); });
		NameTargets<DepthStencilTargetID>("DepthStencil", [](DepthStencilTargetID a_id) { return engine::GetDepthStencilTexture(a_id); });
		if (auto* data = RE::BSGraphics::GetRendererData()) {
			if (auto* texture = data->cubeMapRenderTargets[0].texture)
				annotation::SetName(reinterpret_cast<ID3D11Texture2D*>(texture), "CubeMap/kReflections");
		}
	}

	void OnTechniqueBound(const RE::BSShader* a_shader, std::uint32_t a_pixelShaderId)
	{
		if (!g_installed)
			return;
		t_bound = { a_shader, a_pixelShaderId };
		if (AnnotateDraws())
			annotation::SetMarker(std::format("Draw: {}::{:x}", ShaderLabel(a_shader), a_pixelShaderId));
	}
}
