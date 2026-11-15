#include "Format/PbrValidation.h"

#include "Utils/StringCompare.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace cs::features::true_pbr
{
	namespace
	{
		constexpr std::string_view kPbrPrefix = "pbr";

		bool UnderPbr(std::string_view a_path)
		{
			return a_path.size() > kPbrPrefix.size() && (a_path[kPbrPrefix.size()] == '\\' || a_path[kPbrPrefix.size()] == '/') &&
			       util::EqualsIgnoreCase(a_path.substr(0, kPbrPrefix.size()), kPbrPrefix);
		}

		void CheckRange(std::vector<std::string>& a_errors, std::string_view a_label, const PbrParameter& a_parameter, float a_value)
		{
			if (std::isfinite(a_value) && a_value >= a_parameter.min && a_value <= a_parameter.max)
				return;
			const auto upper = std::isinf(a_parameter.max) ? std::string{} : std::format(" and at most {}", a_parameter.max);
			a_errors.push_back(std::format("{} must be finite, at least {}{} (is {}).", a_label, a_parameter.min, upper, a_value));
		}

		bool AllFinite(const std::array<float, 2>& a_values)
		{
			return std::isfinite(a_values[0]) && std::isfinite(a_values[1]);
		}

		std::string ComponentLabel(const ParameterComponent& a_at)
		{
			if (a_at.parameter->count == 1)
				return std::string(a_at.parameter->label);
			return std::format("{} {}", a_at.parameter->label, "RGB"[a_at.component]);
		}
	}

	Validation Validate(const PbrMaterial& a_material)
	{
		Validation result;
		auto& errors = result.errors;

		if (a_material.payloadVersion != kPayloadVersion)
			errors.push_back(std::format("Payload version must be {} (is {}).", kPayloadVersion, a_material.payloadVersion));

		const auto flags = a_material.featureFlags;
		if ((flags & ~kKnownFeatureMask) != 0)
			errors.emplace_back("Feature flags use bits above 9.");
		for (const auto& feature : kFeatures) {
			if ((flags & feature.mask) == 0)
				continue;
			for (const auto& other : kFeatures) {
				if ((feature.needs & other.mask) != 0 && (flags & other.mask) == 0)
					errors.push_back(std::format("{} needs {}.", feature.label, other.label));
				if ((feature.excludes & other.mask) != 0 && (flags & other.mask) != 0)
					errors.push_back(std::format("{} cannot be combined with {}.", feature.label, other.label));
			}
		}

		const auto resolved = a_material.ResolvedParameters();
		constexpr auto overrideFirst = ParameterIndex("subsurfaceBalance");
		for (std::size_t index = 0; index < kParameterCount; ++index) {
			const auto at = ParameterAt(index);
			const auto label = ComponentLabel(at);
			CheckRange(errors, label, *at.parameter, a_material.parameters[index]);
			// An override replaces the parameter, so it gets the same range.
			if (a_material.thinSurface && index >= overrideFirst && index < overrideFirst + 4)
				CheckRange(errors, "Thin Surface Override " + label, *at.parameter, resolved[index]);
		}
		if (a_material.fuzz && !std::isfinite(a_material.fuzz->roughness))
			errors.emplace_back("Fuzz Override Roughness must be finite.");

		for (std::size_t slot = 0; slot < kTextureSlots.size(); ++slot) {
			const auto& info = kTextureSlots[slot];
			const auto& path = a_material.textures[slot];
			if (path.empty()) {
				if (info.required)
					errors.push_back(std::format("{} texture is required.", info.label));
				else if ((flags & info.requiredByFeature) != 0) {
					const auto owner = std::ranges::find_if(kFeatures, [&](const PbrFeature& a_feature) { return a_feature.mask == info.requiredByFeature; });
					errors.push_back(std::format("{} texture is required by {}.", info.label, owner->label));
				}
			} else if (!UnderPbr(path)) {
				result.warnings.push_back(std::format("{} texture is not under pbr\\ and may replace a vanilla texture.", info.label));
			}
		}

		if (!std::isfinite(a_material.emittanceMult) || a_material.emittanceMult < 0)
			errors.emplace_back("Emissive Scale must be finite and at least 0.");
		if (!std::ranges::all_of(a_material.emittanceColor, [](float a_value) { return std::isfinite(a_value) && a_value >= 0; }))
			errors.emplace_back("Emissive Color must be finite and at least 0.");
		if (!AllFinite(a_material.uvOffset))
			errors.emplace_back("UV Offset must be finite.");
		if (!AllFinite(a_material.uvScale))
			errors.emplace_back("UV Scale must be finite.");
		if (!std::isfinite(a_material.alpha))
			errors.emplace_back("Alpha must be finite.");
		if (!std::isfinite(a_material.refractionPower))
			errors.emplace_back("Refraction Power must be finite.");
		return result;
	}
}
