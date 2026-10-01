#pragma once

#include "Render/ShaderStage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cs::engine
{
	enum class ShaderInjectionTarget : std::uint8_t
	{
		kDeferredPrepass,
		kUtility,
		kParticle,
		kEffect,
		kBloodSplatter,
		kDistantTree,
		kImageSpace,
		kBsSky,
		kBsWater,
		kBsLighting,
		kBsdfLight,
		kBsdfComposite,
		kDfTiledLighting,
		kCount
	};

	constexpr ShaderStageMask ShaderInjectionTargetStages(
		ShaderInjectionTarget a_target) noexcept
	{
		constexpr auto graphics =
			ShaderStageBit(ShaderStage::kVertex) | ShaderStageBit(ShaderStage::kPixel);
		switch (a_target) {
		case ShaderInjectionTarget::kImageSpace:
			return graphics | ShaderStageBit(ShaderStage::kCompute);
		case ShaderInjectionTarget::kDfTiledLighting:
			return ShaderStageBit(ShaderStage::kCompute);
		case ShaderInjectionTarget::kCount:
			return 0;
		default:
			return graphics;
		}
	}

	struct ShaderInjectionTargetMetadata
	{
		ShaderInjectionTarget id = ShaderInjectionTarget::kCount;
		std::string_view name;
		std::string_view label;
		ShaderStageMask supportedStages = ShaderInjectionTargetStages(id);
	};

	inline constexpr std::array<ShaderInjectionTargetMetadata,
		static_cast<std::size_t>(ShaderInjectionTarget::kCount)>
		kShaderInjectionTargets{ { { ShaderInjectionTarget::kDeferredPrepass, "deferred_prepass",
									   "Deferred prepass" },
			{ ShaderInjectionTarget::kUtility, "utility", "Utility" },
			{ ShaderInjectionTarget::kParticle, "particle", "Particle" },
			{ ShaderInjectionTarget::kEffect, "effect", "Effect" },
			{ ShaderInjectionTarget::kBloodSplatter, "blood_splatter",
				"Blood splatter" },
			{ ShaderInjectionTarget::kDistantTree, "distant_tree",
				"Distant tree" },
			{ ShaderInjectionTarget::kImageSpace, "imagespace", "Imagespace" },
			{ ShaderInjectionTarget::kBsSky, "bssky", "BSSky" },
			{ ShaderInjectionTarget::kBsWater, "bswater", "BSWater" },
			{ ShaderInjectionTarget::kBsLighting, "bslighting", "BSLighting" },
			{ ShaderInjectionTarget::kBsdfLight, "bsdf_light", "BSDF light" },
			{ ShaderInjectionTarget::kBsdfComposite, "bsdf_composite",
				"BSDF composite" },
			{ ShaderInjectionTarget::kDfTiledLighting, "df_tiled_lighting",
				"DFTiledLighting" } } };

	inline std::span<const ShaderInjectionTargetMetadata>
	GetShaderInjectionTargets() noexcept
	{
		return kShaderInjectionTargets;
	}

	inline constexpr const ShaderInjectionTargetMetadata* GetShaderInjectionTarget(
		ShaderInjectionTarget a_target) noexcept
	{
		const auto index = static_cast<std::size_t>(a_target);
		return index < kShaderInjectionTargets.size() ?
		           &kShaderInjectionTargets[index] :
		           nullptr;
	}

	inline const ShaderInjectionTargetMetadata* FindShaderInjectionTarget(
		std::string_view a_name) noexcept
	{
		for (const auto& target : kShaderInjectionTargets) {
			if (target.name == a_name)
				return &target;
		}
		return nullptr;
	}
}
