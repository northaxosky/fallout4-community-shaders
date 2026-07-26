#pragma once

#include <optional>
#include <string_view>

namespace cs::features
{
	enum class SssInjectionMode
	{
		kStock,
		kDxbcPatch,
		kHlslReconstruction
	};

	struct SssStartupDecision
	{
		bool runLifecycle = false;
		bool injectionReady = false;
		bool routeFallsBackToStock = false;
	};

	inline constexpr SssStartupDecision DecideSssStartup(
		SssInjectionMode a_mode,
		bool a_injectionReady) noexcept
	{
		return {
			.runLifecycle = a_mode != SssInjectionMode::kStock,
			.injectionReady = a_injectionReady,
			.routeFallsBackToStock =
				a_mode != SssInjectionMode::kStock
				&& !a_injectionReady
		};
	}

	inline constexpr std::string_view SssInjectionModeName(
		SssInjectionMode a_mode) noexcept
	{
		switch (a_mode) {
		case SssInjectionMode::kStock:
			return "stock";
		case SssInjectionMode::kDxbcPatch:
			return "dxbc_patch";
		case SssInjectionMode::kHlslReconstruction:
			return "hlsl_reconstruction";
		}
		return "stock";
	}

	inline constexpr std::optional<SssInjectionMode> ParseSssInjectionMode(
		std::string_view a_value) noexcept
	{
		if (a_value == "stock")
			return SssInjectionMode::kStock;
		if (a_value == "dxbc_patch")
			return SssInjectionMode::kDxbcPatch;
		if (a_value == "hlsl_reconstruction")
			return SssInjectionMode::kHlslReconstruction;
		return std::nullopt;
	}

	inline constexpr std::string_view SssPatchStatusName(
		bool a_artifactLoaded) noexcept
	{
		return a_artifactLoaded ? "provisional" : "unavailable";
	}
}
