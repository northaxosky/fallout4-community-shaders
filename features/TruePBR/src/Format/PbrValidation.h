#pragma once

#include "Format/PbrMaterial.h"

#include <string>
#include <vector>

namespace cs::features::true_pbr
{
	struct Validation
	{
		std::vector<std::string> errors;
		std::vector<std::string> warnings;
	};

	// The schema's rules, whatever file format produced the material.
	// A material with errors must not be published.
	[[nodiscard]] Validation Validate(const PbrMaterial& a_material);
}
