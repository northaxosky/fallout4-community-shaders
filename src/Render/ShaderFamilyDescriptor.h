#pragma once

#include "Render/ShaderInjection.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace cs::engine
{
	[[nodiscard]] std::string ProfileForStage(ShaderStage a_stage);
	[[nodiscard]] std::filesystem::path GetShaderPath(std::string_view a_nativeName);
	[[nodiscard]] bool IsShaderSourceAvailable(
		const std::filesystem::path& a_shaderRoot, std::string_view a_nativeName);

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

	[[nodiscard]] std::optional<ShaderVariantCompilationDescriptor>
	BuildShaderFamilyCompilationDescriptor(
		const ShaderFamilyDescriptor& a_descriptor);
}
