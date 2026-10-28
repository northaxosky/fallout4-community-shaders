#pragma once

#include "Render/GameRuntime.h"
#include "Render/ShaderFamilyDescriptor.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cs::engine
{
	// One stock shader variant of the identity corpus; owns its descriptor strings.
	struct StockShaderRow
	{
		// Table spelling; targets we do not inject resolve to kCount.
		std::string targetName;
		ShaderInjectionTarget target = ShaderInjectionTarget::kCount;
		ShaderStage stage = ShaderStage::kPixel;
		std::uint32_t descriptor = 0;
		bool forceEarlyDepthStencil = false;
		std::string expected;
		std::uint32_t ordinal = 0;
		std::string nativeName;
		std::string nativeClassName;
		std::string nativeSourceGroup;
		ShaderInjectionDefines nativeMacros;

		// Views this row's strings; valid while the row is unmoved.
		[[nodiscard]] ShaderFamilyDescriptor Family() const;
	};

	struct StockShaderTable
	{
		std::map<std::string, std::string> header;
		GameRuntime runtime = GameRuntime::kAE;
		// The table lists only targets that differ from the AE table.
		bool inheritsTargets = false;
		std::vector<StockShaderRow> rows;
	};

	// Throws std::runtime_error on a malformed table.
	[[nodiscard]] StockShaderTable ParseStockShaderTable(std::string_view a_text);
}
