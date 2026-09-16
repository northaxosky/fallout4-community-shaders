#pragma once

#include "Render/ShaderInjection.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::engine
{
	struct ShaderFamilyDescriptor
	{
		ShaderInjectionTarget target = ShaderInjectionTarget::kCount;
		ShaderStage stage = ShaderStage::kPixel;
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
