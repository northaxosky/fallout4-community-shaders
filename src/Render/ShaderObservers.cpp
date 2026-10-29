#include "Render/ShaderObservers.h"

#include "PCH.h"
#include "Render/ShaderSubclassHooks.h"

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

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace cs::engine
{
	namespace
	{
		constexpr std::size_t kMaxObservers = 4;
		constexpr std::size_t kTargetCount = static_cast<std::size_t>(ShaderInjectionTarget::kCount);

		template <class Observer>
		struct Observers
		{
			std::array<Observer, kMaxObservers> list{};
			std::size_t count = 0;
			bool hooked = false;
		};
		std::array<Observers<GeometryObserver>, kTargetCount> g_geometryObservers;
		std::array<Observers<TechniqueObserver>, kTargetCount> g_techniqueObservers;

		template <ShaderInjectionTarget Target>
		struct SetupGeometryHook
		{
			static constexpr std::size_t size = 0x07;

			static void thunk(RE::BSShader* a_self, RE::BSRenderPass* a_pass)
			{
				const auto& observers = g_geometryObservers[static_cast<std::size_t>(Target)];
				for (std::size_t i = 0; i < observers.count; ++i)
					observers.list[i].setup(a_self, a_pass);
				func(a_self, a_pass);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <ShaderInjectionTarget Target>
		struct RestoreGeometryHook
		{
			static constexpr std::size_t size = 0x08;

			static void thunk(RE::BSShader* a_self, RE::BSRenderPass* a_pass)
			{
				func(a_self, a_pass);
				const auto& observers = g_geometryObservers[static_cast<std::size_t>(Target)];
				for (std::size_t i = observers.count; i-- > 0;)
					observers.list[i].restore(a_self, a_pass);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <ShaderInjectionTarget Target>
		struct SetupTechniqueHook
		{
			static constexpr std::size_t size = 0x02;

			static bool thunk(RE::BSShader* a_self, std::uint32_t a_technique)
			{
				const auto& observers = g_techniqueObservers[static_cast<std::size_t>(Target)];
				for (std::size_t i = 0; i < observers.count; ++i)
					observers.list[i].setup(a_self, a_technique);
				return func(a_self, a_technique);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct GeometryHooks
		{
			template <class Shader, ShaderInjectionTarget Target>
			static void Install()
			{
				stl::write_vfunc<Shader, SetupGeometryHook<Target>>();
				stl::write_vfunc<Shader, RestoreGeometryHook<Target>>();
			}
		};

		struct TechniqueHooks
		{
			template <class Shader, ShaderInjectionTarget Target>
			static void Install()
			{
				stl::write_vfunc<Shader, SetupTechniqueHook<Target>>();
			}
		};

		template <class Hooks, class Observer>
		void Observe(std::array<Observers<Observer>, kTargetCount>& a_lists, ShaderInjectionTarget a_target, Observer a_observer, const char* a_overflow)
		{
			auto& entry = a_lists.at(static_cast<std::size_t>(a_target));
			if (entry.count == kMaxObservers)
				throw std::length_error(a_overflow);
			entry.list[entry.count++] = a_observer;
			if (std::exchange(entry.hooked, true))
				return;
			switch (a_target) {
#define CS_HOOK_SHADER(klass, target)                                        \
	case ShaderInjectionTarget::target:                                      \
		Hooks::template Install<RE::klass, ShaderInjectionTarget::target>(); \
		break
				CS_SHADER_SUBCLASSES(CS_HOOK_SHADER);
#undef CS_HOOK_SHADER
			default:
				break;
			}
		}
	}

	void ObserveShaderGeometry(ShaderInjectionTarget a_target, GeometryObserver a_observer)
	{
		Observe<GeometryHooks>(g_geometryObservers, a_target, a_observer, "Too many geometry observers");
	}

	void ObserveShaderTechnique(ShaderInjectionTarget a_target, TechniqueObserver a_observer)
	{
		Observe<TechniqueHooks>(g_techniqueObservers, a_target, a_observer, "Too many technique observers");
	}
}
