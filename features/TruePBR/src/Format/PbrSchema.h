#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

// Mirrors TruePbrSchema.cs of the Material Editor library (the reference writer).
namespace cs::features::true_pbr
{
	inline constexpr std::uint32_t kCspbLayout = 1;
	inline constexpr std::uint32_t kPayloadVersion = 2;
	inline constexpr float kUnbounded = std::numeric_limits<float>::infinity();

	struct PbrParameter
	{
		std::string_view name;
		std::string_view label;
		std::uint8_t count;  // components; colors take three
		float defaultValue;
		float min;
		float max;
	};

	// TruePBR 2.0 order, defaults and ranges.
	inline constexpr std::array kParameters{
		PbrParameter{ "roughnessScale", "Roughness Scale", 1, 1.0f, 0.0f, kUnbounded },
		PbrParameter{ "specularLevel", "Specular Level", 1, 0.04f, 0.0f, 1.0f },
		PbrParameter{ "displacementScale", "Displacement Scale", 1, 1.0f, 0.0f, kUnbounded },
		PbrParameter{ "subsurfaceColor", "Subsurface Color", 3, 1.0f, 0.0f, kUnbounded },
		PbrParameter{ "subsurfaceOpacity", "Subsurface Opacity", 1, 0.0f, 0.0f, 1.0f },
		PbrParameter{ "coatColor", "Coat Color", 3, 1.0f, 0.0f, kUnbounded },
		PbrParameter{ "coatStrength", "Coat Strength", 1, 1.0f, 0.0f, 1.0f },
		PbrParameter{ "coatRoughness", "Coat Roughness", 1, 1.0f, 0.0f, 1.0f },
		PbrParameter{ "coatSpecularLevel", "Coat Specular Level", 1, 0.04f, 0.0f, 1.0f },
		PbrParameter{ "fuzzColor", "Fuzz Color", 3, 1.0f, 0.0f, kUnbounded },
		PbrParameter{ "fuzzWeight", "Fuzz Weight", 1, 0.0f, 0.0f, 1.0f },
		PbrParameter{ "glintScreenSpaceScale", "Glint Screen Space Scale", 1, 1.5f, 1.0f, kUnbounded },
		PbrParameter{ "glintLogMicrofacetDensity", "Glint Log Microfacet Density", 1, 40.0f, 1.0f, 40.0f },
		PbrParameter{ "glintMicrofacetRoughness", "Glint Microfacet Roughness", 1, 0.015f, 0.005f, 0.3f },
		PbrParameter{ "glintDensityRandomization", "Glint Density Randomization", 1, 2.0f, 0.0f, 5.0f },
		PbrParameter{ "subsurfaceBalance", "Subsurface Balance", 1, 0.75f, 0.0f, 1.0f },
		PbrParameter{ "thinSurfaceFrontSpecularWeight", "Thin Surface Front Specular Weight", 1, 0.5f, 0.0f, 1.0f },
		PbrParameter{ "thinSurfaceBackSpecularWeight", "Thin Surface Back Specular Weight", 1, 0.5f, 0.0f, 1.0f },
		PbrParameter{ "thinSurfaceBackRoughnessScale", "Thin Surface Back Roughness Scale", 1, 1.0f, 0.0f, 3.0f }
	};

	consteval std::size_t CountParameterComponents()
	{
		std::size_t total = 0;
		for (const auto& parameter : kParameters)
			total += parameter.count;
		return total;
	}

	inline constexpr std::size_t kParameterCount = CountParameterComponents();
	static_assert(kParameterCount == 25);

	// Index of a parameter's first component in the 25-float array.
	consteval std::size_t ParameterIndex(std::string_view a_name)
	{
		std::size_t index = 0;
		for (const auto& parameter : kParameters) {
			if (parameter.name == a_name)
				return index;
			index += parameter.count;
		}
		throw "unknown parameter";
	}

	struct ParameterComponent
	{
		const PbrParameter* parameter;
		std::size_t component;
	};

	constexpr ParameterComponent ParameterAt(std::size_t a_index)
	{
		for (const auto& parameter : kParameters) {
			if (a_index < parameter.count)
				return { &parameter, a_index };
			a_index -= parameter.count;
		}
		return { nullptr, 0 };
	}

	namespace feature
	{
		inline constexpr std::uint32_t kSubsurface = 1u << 0;
		inline constexpr std::uint32_t kTwoLayer = 1u << 1;
		inline constexpr std::uint32_t kColoredCoat = 1u << 2;
		inline constexpr std::uint32_t kInterlayerParallax = 1u << 3;
		inline constexpr std::uint32_t kCoatNormal = 1u << 4;
		inline constexpr std::uint32_t kFuzz = 1u << 5;
		inline constexpr std::uint32_t kHairMarschner = 1u << 6;
		inline constexpr std::uint32_t kGlint = 1u << 7;
		inline constexpr std::uint32_t kFoliage = 1u << 8;
		inline constexpr std::uint32_t kThinSurface = 1u << 9;
	}

	struct PbrFeature
	{
		std::string_view label;
		std::uint32_t mask;
		std::uint32_t needs;
		std::uint32_t excludes;
	};

	inline constexpr std::array kFeatures{
		PbrFeature{ "Subsurface", feature::kSubsurface, 0, 0 },
		PbrFeature{ "Two Layer", feature::kTwoLayer, 0,
			feature::kSubsurface | feature::kFuzz | feature::kGlint | feature::kFoliage | feature::kThinSurface },
		PbrFeature{ "Colored Coat", feature::kColoredCoat, feature::kTwoLayer, 0 },
		PbrFeature{ "Interlayer Parallax", feature::kInterlayerParallax, feature::kTwoLayer, 0 },
		PbrFeature{ "Coat Normal", feature::kCoatNormal, feature::kTwoLayer, 0 },
		PbrFeature{ "Fuzz", feature::kFuzz, 0, feature::kGlint },
		PbrFeature{ "Hair Marschner", feature::kHairMarschner, 0,
			feature::kSubsurface | feature::kTwoLayer | feature::kColoredCoat | feature::kInterlayerParallax |
				feature::kCoatNormal | feature::kFuzz | feature::kGlint | feature::kFoliage | feature::kThinSurface },
		PbrFeature{ "Glint", feature::kGlint, 0, 0 },
		PbrFeature{ "Foliage", feature::kFoliage, feature::kThinSurface, 0 },
		PbrFeature{ "Thin Surface", feature::kThinSurface, 0, 0 }
	};

	inline constexpr std::uint32_t kKnownFeatureMask = (1u << kFeatures.size()) - 1;

	// Slot order of the CSPB texture list and of PbrMaterial::textures.
	enum class PbrTexture : std::uint8_t
	{
		kBaseColor,
		kNormal,
		kEmissive,
		kDisplacement,
		kRmaos,
		kFeatures0,
		kFeatures1
	};

	struct PbrTextureSlot
	{
		std::string_view name;
		std::string_view label;
		bool required;
		std::uint32_t requiredByFeature;  // feature mask, or 0
	};

	inline constexpr std::array kTextureSlots{
		PbrTextureSlot{ "baseColor", "Base Color", true, 0 },
		PbrTextureSlot{ "normal", "Normal", true, 0 },
		PbrTextureSlot{ "emissive", "Emissive", false, 0 },
		PbrTextureSlot{ "displacement", "Displacement", false, feature::kInterlayerParallax },
		PbrTextureSlot{ "rmaos", "RMAOS", true, 0 },
		PbrTextureSlot{ "features0", "Features 0", false, 0 },
		PbrTextureSlot{ "features1", "Features 1", false, feature::kCoatNormal }
	};
}
