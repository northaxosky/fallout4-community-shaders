#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace cs::features::catalog
{
	// DXBC structural fingerprint from D3DReflect + D3DDisassemble; unset optionals map to SQL NULL, and extracted==false means both failed.
	struct ShaderShape
	{
		bool extracted = false;
		std::optional<std::string> profile;
		std::optional<int>         cb_count;
		std::optional<int>         srv_count;
		std::optional<int>         uav_count;
		std::optional<int>         sampler_count;
		std::optional<int>         output_count;
		std::optional<int>         input_count;
		std::optional<int>         input_has_position_only;  // 0/1; NULL if signature not inspectable
		std::optional<int>         instruction_count;
		std::optional<int>         sample_call_count;
		std::optional<std::string> input_signature_summary;
		std::optional<std::string> output_signature_summary;
		std::optional<std::string> resource_summary;
	};

	// Reflect and disassemble DXBC into ShaderShape without a device or COM apartment; thread-safe and false only if both passes fail.
	bool ExtractShaderShape(const void* dxbc, std::size_t len, ShaderShape& out);
}
