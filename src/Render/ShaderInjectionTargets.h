#pragma once

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
		kFaceCustomization,
		kImageSpace,
		kBsSky,
		kBsWater,
		kBsLighting,
		kBsdfLight,
		kBsdfComposite,
		kDfTiledLighting,
		kCount
	};

	struct ShaderInjectionDefineMetadata
	{
		std::string_view name;
		std::string_view value;
	};

	struct ShaderInjectionTargetMetadata
	{
		ShaderInjectionTarget                          id = ShaderInjectionTarget::kCount;
		std::string_view                               name;
		std::string_view                               label;
		std::wstring_view                              sourcePath;
		std::string_view                               entryPoint;
		std::string_view                               profile;
		std::span<const ShaderInjectionDefineMetadata> baseDefines;
	};

	inline constexpr std::array<ShaderInjectionDefineMetadata, 0>
		kNoShaderInjectionDefines{};
	inline constexpr std::array<ShaderInjectionTargetMetadata,
		static_cast<std::size_t>(ShaderInjectionTarget::kCount)>
		kShaderInjectionTargets{ {
			{ ShaderInjectionTarget::kDeferredPrepass, "deferred_prepass",
				"Deferred prepass", L"Stock/prepass_ps.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kUtility, "utility", "Utility",
				L"Stock/utility_ps.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kParticle, "particle", "Particle",
				L"Stock/particle.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kEffect, "effect", "Effect",
				L"Stock/effect.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kBloodSplatter, "blood_splatter",
				"Blood splatter", L"Stock/blood_splatter_ps.hlsl", "main",
				"ps_5_0", kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kDistantTree, "distant_tree",
				"Distant tree", L"Stock/distant_tree_ps.hlsl", "main",
				"ps_5_0", kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kFaceCustomization, "face_customization",
				"Face customization", L"Stock/face_customization_vs.hlsl",
				"main", "vs_5_0", kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kImageSpace, "imagespace", "Imagespace",
				L"Stock/vls_slice_scatter_ps.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kBsSky, "bssky", "BSSky",
				L"BSSkyShader.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kBsWater, "bswater", "BSWater",
				L"BSWaterShader.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kBsLighting, "bslighting", "BSLighting",
				L"BSLightingShader.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kBsdfLight, "bsdf_light", "BSDF light",
				L"BSDFLightShader.hlsl", "main", "ps_5_0",
				kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kBsdfComposite, "bsdf_composite",
				"BSDF composite", L"BSDFCompositeShader.hlsl", "main",
				"ps_5_0", kNoShaderInjectionDefines },
			{ ShaderInjectionTarget::kDfTiledLighting, "df_tiled_lighting",
				"DFTiledLighting", L"DFTiledLighting.hlsl", "main", "cs_5_0",
				kNoShaderInjectionDefines }
		} };

	inline std::span<const ShaderInjectionTargetMetadata>
		GetShaderInjectionTargets() noexcept
	{
		return kShaderInjectionTargets;
	}

	inline const ShaderInjectionTargetMetadata* GetShaderInjectionTarget(
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
