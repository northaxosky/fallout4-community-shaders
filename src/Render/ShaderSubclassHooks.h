#pragma once

#include <cstddef>
#include <cstdint>

namespace cs::engine
{
	struct ShaderSubclassHookInstallStats
	{
		unsigned attempted = 0;
		unsigned succeeded = 0;
		unsigned failed = 0;
	};

	struct ShaderSubclassHookRuntimeStats
	{
		std::uint64_t setupTechniqueCalls = 0;
	};

	struct ShaderSubclassRuntimeLayout
	{
		bool verified = false;
		std::size_t pixelShadersOffset = 0;
	};

	using ShaderSubclassSetupObserver = void (*)(
		void* a_shader,
		const char* a_subclassName,
		std::uint32_t a_techniqueBits) noexcept;

	void InstallShaderSubclassHooks();
	bool RegisterShaderSubclassSetupObserver(
		ShaderSubclassSetupObserver a_observer) noexcept;
	ShaderSubclassRuntimeLayout GetShaderSubclassRuntimeLayout() noexcept;
	ShaderSubclassHookInstallStats GetReloadShaderHookInstallStats() noexcept;
	ShaderSubclassHookInstallStats GetSetupTechniqueHookInstallStats() noexcept;
	ShaderSubclassHookRuntimeStats GetShaderSubclassHookRuntimeStats() noexcept;
}
