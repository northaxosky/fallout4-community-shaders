#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace cs::features::catalog
{
	// Structural fingerprint extracted from a shader's DXBC via D3DReflect + D3DDisassemble.
	// Unset optionals map to SQL NULL; extracted==false means reflect AND disasm both failed.
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

	// Reflect + disassemble a DXBC blob into a ShaderShape. Device-free and thread-safe
	// (no COM apartment required). Returns false only when both passes fail.
	bool ExtractShaderShape(const void* dxbc, std::size_t len, ShaderShape& out);
}
