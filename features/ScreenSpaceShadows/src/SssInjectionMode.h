#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace cs::features
{
	enum class SssInjectionMode
	{
		kStock,
		kHlslReconstruction
	};

	struct SssInjectionModeOption
	{
		SssInjectionMode mode;
		std::string_view name;
		const char*      label;
	};

	inline constexpr std::array<SssInjectionModeOption, 2>
		kSssInjectionModeOptions{ {
			{
				SssInjectionMode::kStock,
				"stock",
				"Stock (no substitution)"
			},
			{
				SssInjectionMode::kHlslReconstruction,
				"hlsl_reconstruction",
				"HLSL reconstruction (developer validation)"
			}
		} };

	inline constexpr std::string_view kRetiredSssInjectionModeName =
		"dxbc_patch";

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
		for (const auto& option : kSssInjectionModeOptions) {
			if (option.mode == a_mode)
				return option.name;
		}
		return "stock";
	}

	inline constexpr std::optional<SssInjectionMode> ParseSssInjectionMode(
		std::string_view a_value) noexcept
	{
		for (const auto& option : kSssInjectionModeOptions) {
			if (option.name == a_value)
				return option.mode;
		}
		return std::nullopt;
	}

	struct SssInjectionModeSettingParseResult
	{
		std::optional<SssInjectionMode> mode;
		bool migratedRetiredMode = false;
	};

	inline constexpr SssInjectionModeSettingParseResult
		ParseSssInjectionModeSetting(std::string_view a_value) noexcept
	{
		if (const auto mode = ParseSssInjectionMode(a_value))
			return { .mode = mode };
		if (a_value == kRetiredSssInjectionModeName) {
			return {
				.mode = SssInjectionMode::kStock,
				.migratedRetiredMode = true
			};
		}
		return {};
	}
}
