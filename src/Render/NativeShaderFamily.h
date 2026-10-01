#pragma once

#include "RE/B/BSShaderManager.h"
#include "Render/ShaderInjectionTargets.h"

#include <optional>
#include <string_view>

namespace cs::engine
{
	[[nodiscard]] inline std::optional<ShaderInjectionTarget> ResolveGraphicsShaderTarget(
		RE::BSShaderManager::ShaderEnum a_type, std::string_view a_name) noexcept
	{
		using Type = RE::BSShaderManager::ShaderEnum;
		switch (a_type) {
		case Type::kEffect:
			return ShaderInjectionTarget::kEffect;
		case Type::kUtility:
			return ShaderInjectionTarget::kUtility;
		case Type::kDistantTree:
			return ShaderInjectionTarget::kDistantTree;
		case Type::kParticle:
			return ShaderInjectionTarget::kParticle;
		case Type::kDFPrepass:
			// FO4's DFPrepass and DFLight constructors both store type 4.
			if (a_name == "DFLight")
				return ShaderInjectionTarget::kBsdfLight;
			if (a_name == "DFPrepass")
				return ShaderInjectionTarget::kDeferredPrepass;
			return std::nullopt;
		case Type::kDFComposite:
			return ShaderInjectionTarget::kBsdfComposite;
		case Type::kBloodSpatter:
			return ShaderInjectionTarget::kBloodSplatter;
		case Type::kImageSpace:
			return ShaderInjectionTarget::kImageSpace;
		case Type::kSky:
			return ShaderInjectionTarget::kBsSky;
		case Type::kWater:
			return ShaderInjectionTarget::kBsWater;
		case Type::kLighting:
			return ShaderInjectionTarget::kBsLighting;
		default:
			return std::nullopt;
		}
	}

	[[nodiscard]] inline std::optional<ShaderInjectionTarget> ResolveStandaloneComputeTarget(
		std::string_view a_name) noexcept
	{
		if (a_name == "DFTiledLighting")
			return ShaderInjectionTarget::kDfTiledLighting;
		if (a_name == "IndexBufferOffsetCS")
			return ShaderInjectionTarget::kImageSpace;
		return std::nullopt;
	}
}
