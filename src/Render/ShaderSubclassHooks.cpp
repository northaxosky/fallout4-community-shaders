#include "Render/ShaderSubclassHooks.h"
#include "RE/B/BSComputeShader.h"
#include "RE/B/BSIStream.h"

#include "Log.h"
#include "PCH.h"
#include "Render/Engine.h"
#include "Render/FrameAnnotations.h"
#include "Render/FrameProfiler.h"
#include "Render/NativeShaderFamily.h"
#include "Render/ShaderHookSupport.h"
#include "Render/ShaderInjection.h"
#include "Render/SkyReflectionCube.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <exception>
#include <format>
#include <mutex>
#include <string_view>

#include "RE/B/BSGeometry.h"
#include "RE/B/BSRenderPass.h"
#include "RE/B/BSSkyShader.h"

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shadersubclass");

		std::once_flag g_installOnce;

		struct ActiveBeginTechnique
		{
			RE::BSShader* shader = nullptr;
			std::uint32_t vertexShaderId = 0;
			std::uint32_t pixelShaderId = 0;
		};

		thread_local const ActiveBeginTechnique* t_activeBeginTechnique =
			nullptr;

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
					const auto* name = a_shader->GetFxpFilename();
					render::profiling::SetShaderFamily(a_shader->shaderType, name ? name : "Unknown");
					render::frame_annotations::OnTechniqueBound(a_shader, a_pixelShaderId);
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
					ClearBoundShaderBinding();
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
				RE::BSComputeShader* a_owner,
				RE::BSIStream* a_stream)
			{
				const bool streamIsGood = a_stream && a_stream->IsGood();
				const auto result = func(a_owner, a_stream);
				const std::string_view name =
					a_owner->name ?
						a_owner->name :
						"";
				if (const auto target = ResolveStandaloneComputeTarget(name)) {
					ObserveNativeComputeOwner(
						a_owner,
						*target,
						name,
						streamIsGood);
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
				const bool streamIsGood = a_stream && a_stream->IsGood();
				const auto result = func(a_shader, a_stream);
				ObserveNativeShader(a_shader, streamIsGood);
				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		thread_local const RE::BSRenderPass* t_skyPass = nullptr;

		// A sky draw follows its SetupGeometry, so draw hooks can read the pass.
		struct SkySetupGeometryHook
		{
			static constexpr std::size_t size = 0x07;

			static void thunk(RE::BSShader* a_self, RE::BSRenderPass* a_pass)
			{
				t_skyPass = a_pass;
				// Sky techniques never write the cull selector.
				ReselectSkyReflectionCubeCull();
				func(a_self, a_pass);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	const RE::BSRenderPass* GetCurrentSkyPass() noexcept
	{
		return t_skyPass;
	}

	void InstallShaderSubclassHooks()
	{
		std::call_once(g_installOnce, [] {
			TryPatch("BSShader archive loader observer", [] {
				stl::detour_thunk<ReloadFromStreamHook>(RE::ID::BSShader::Load);
			});
			TryPatch("native graphics shader-set boundary", [] {
				stl::detour_thunk<SetShadersHook>(RE::ID::BSGraphics::Renderer::SetShaders);
			});
			TryPatch("native compute shader-run boundary", [] {
				stl::detour_thunk<RunComputeShaderHook>(RE::ID::BSGraphics::Renderer::RunComputeShader);
			});
			TryPatch("standalone compute shader observers", [] {
				stl::detour_thunk<ReloadStandaloneComputeHook>(RE::ID::BSComputeShader::LoadTechniquesFromStream);
			});
			TryPatch("BSSkyShader SetupGeometry pass", [] { stl::write_vfunc<RE::BSSkyShader, SkySetupGeometryHook>(); });
			TryPatch("BSShader native technique binder", [] {
				stl::detour_thunk<BeginTechniqueHook>(RE::ID::BSShader::BeginTechnique);
			});
		});
	}
}
