#pragma once

#include "Render/ShaderInjection.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace cs::engine
{
	[[nodiscard]] std::string ProfileForStage(ShaderStage a_stage);
	[[nodiscard]] std::filesystem::path GetShaderPath(std::string_view a_sourceName);
	[[nodiscard]] bool IsShaderSourceAvailable(
		const std::filesystem::path& a_shaderRoot, std::string_view a_sourceName);

	struct ShaderFamilyDescriptor
	{
		ShaderInjectionTarget target = ShaderInjectionTarget::kCount;
		ShaderStage stage = ShaderStage::kPixel;
		// Native stage-map ID, not the raw SetupTechnique value.
		std::uint32_t descriptor = 0;
		std::string_view nativeName;
		std::string_view nativeClassName;
		std::string_view nativeSourceGroup;
		bool forceEarlyDepthStencil = false;
		ShaderInjectionDefines nativeMacros;
	};

	// BSDFLight families that render sun cascades; unshadowed passes do not.
	inline constexpr std::array<std::string_view, 3> kDirectionalSunShadowedFamilies{
		"BSDFLIGHT_PS_DIRSPLITS1", "BSDFLIGHT_PS_DIRSPLITS2", "BSDFLIGHT_PS_DIRSPLITS3"
	};

	// Active BSDFLight variant is a directional family with sun cascades.
	[[nodiscard]] inline bool ActiveLightHasSunShadowedFamily() noexcept
	{
		constexpr auto target = ShaderInjectionTarget::kBsdfLight;
		return ActiveShaderInjectionVariantHasDefine(target, "DIRECTIONAL") &&
		       ActiveShaderInjectionVariantHasAnyDefine(target, kDirectionalSunShadowedFamilies);
	}

	// BSDFComposite families that sample authored envmaps; never decals.
	inline constexpr std::array<std::string_view, 5> kCompositeEnvironmentFamilies{
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
		"BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY",
		"BSDFCOMPOSITE_PS_CUBE_IBL"
	};

	// Active composite reflects Vanilla PBR's live cube at its envmap sites.
	[[nodiscard]] inline bool ActiveCompositeHasLiveEnvironment() noexcept
	{
		constexpr auto target = ShaderInjectionTarget::kBsdfComposite;
		return ActiveShaderInjectionVariantHasAnyDefine(target, kCompositeEnvironmentFamilies) &&
		       ActiveShaderInjectionVariantHasDefine(target, "VANILLA_PBR") &&
		       ActiveShaderInjectionVariantHasDefine(target, "DYNAMIC_CUBEMAPS");
	}

	// Source file that reconstructs a native shader; usually its own name.
	[[nodiscard]] std::string_view ResolveShaderSourceName(
		const ShaderFamilyDescriptor& a_descriptor) noexcept;

	[[nodiscard]] std::optional<ShaderVariantCompilationDescriptor>
	BuildShaderFamilyCompilationDescriptor(
		const ShaderFamilyDescriptor& a_descriptor);
}
