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
#include <stdexcept>
#include <utility>

namespace cs::engine
{
	namespace
	{
		constexpr std::size_t kMaxObservers = 4;
		constexpr std::size_t kTargetCount = static_cast<std::size_t>(ShaderInjectionTarget::kCount);

		struct Observers
		{
			std::array<GeometryObserver, kMaxObservers> list{};
			std::size_t count = 0;
			bool hooked = false;
		};
		std::array<Observers, kTargetCount> g_geometryObservers;

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

		template <class Shader, ShaderInjectionTarget Target>
		void InstallGeometryHooks()
		{
			stl::write_vfunc<Shader, SetupGeometryHook<Target>>();
			stl::write_vfunc<Shader, RestoreGeometryHook<Target>>();
		}
	}

	void ObserveShaderGeometry(ShaderInjectionTarget a_target, GeometryObserver a_observer)
	{
		auto& entry = g_geometryObservers.at(static_cast<std::size_t>(a_target));
		if (entry.count == kMaxObservers)
			throw std::length_error("Too many geometry observers");
		entry.list[entry.count++] = a_observer;
		if (std::exchange(entry.hooked, true))
			return;
		switch (a_target) {
#define CS_HOOK_SHADER(klass, target)                                     \
	case ShaderInjectionTarget::target:                                   \
		InstallGeometryHooks<RE::klass, ShaderInjectionTarget::target>(); \
		break
			CS_SHADER_SUBCLASSES(CS_HOOK_SHADER);
#undef CS_HOOK_SHADER
		default:
			break;
		}
	}
}
