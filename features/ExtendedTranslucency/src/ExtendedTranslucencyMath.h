#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cs::features::extended_translucency
{
	enum class MaterialModel : std::uint32_t
	{
		kDisabled,
		kRimLight,
		kIsotropicFabric,
		kAnisotropicFabric
	};

	enum class ClassificationSource : std::uint32_t
	{
		kNone,
		kExtraData,
		kMaterialName
	};

	enum class ClassificationOutcome : std::uint32_t
	{
		kNotAlphaBlended,
		kExtraDataActive,
		kExtraDataDisabled,
		kExtraDataInvalid,
		kMaterialNameActive,
		kMaterialNameMiss
	};

	inline constexpr std::uint32_t kDescriptorUseDefault = 0;
	inline constexpr std::uint32_t kDescriptorDisabled = 7;
	inline constexpr std::uint32_t kMaterialMask = 7;
	inline constexpr std::uint32_t kDescriptorShift = 3;
	inline constexpr std::uint32_t kSourceShift = 6;
	inline constexpr std::uint32_t kDebugFlag = 1U << 8;
	inline constexpr float kMinimumAlpha = 0.0156862754f;
	using TangentBasis = std::array<std::array<float, 3>, 3>;

	inline constexpr std::array<std::string_view, 5>
		kDefaultFallbackMaterialNames{
			"DiamondCloth02Alpha.BGSM",
			"DiamondClothPlain02Alpha.BGSM",
			"ClothFlag01Alpha.BGSM",
			"FlagMinutemen01Backlit.BGSM",
			"PrewarFlag01.BGSM"
		};

	inline std::vector<std::string> DefaultFallbackMaterialNames()
	{
		std::vector<std::string> names;
		names.reserve(kDefaultFallbackMaterialNames.size());
		for (const auto name : kDefaultFallbackMaterialNames)
			names.emplace_back(name);
		return names;
	}

	struct Settings
	{
		MaterialModel materialModel = MaterialModel::kAnisotropicFabric;
		float alphaReduction = 0.15f;
		float alphaSoftness = 0.0f;
		float alphaStrength = 0.0f;
		std::vector<std::string> fallbackMaterialNames =
			DefaultFallbackMaterialNames();
	};

	struct ClassificationInput
	{
		bool alphaBlended = false;
		bool hasExtraData = false;
		bool extraDataIsInteger = false;
		bool fallbackEligible = true;
		std::int32_t extraDataValue = 0;
		std::string_view rootMaterialName;
	};

	struct DrawClassification
	{
		std::uint32_t descriptor = kDescriptorDisabled;
		ClassificationSource source = ClassificationSource::kNone;
		ClassificationOutcome outcome =
			ClassificationOutcome::kNotAlphaBlended;
	};

	inline char LowerAscii(char a_value) noexcept
	{
		return a_value >= 'A' && a_value <= 'Z' ?
			static_cast<char>(a_value - 'A' + 'a') :
			a_value;
	}

	inline std::string NormalizeMaterialName(std::string_view a_name)
	{
		const auto separator = a_name.find_last_of("\\/");
		if (separator != std::string_view::npos)
			a_name.remove_prefix(separator + 1);

		std::string normalized;
		normalized.reserve(a_name.size());
		for (const char value : a_name)
			normalized.push_back(LowerAscii(value));
		return normalized;
	}

	inline bool MatchesFallbackMaterial(
		std::string_view a_rootMaterialName,
		std::span<const std::string> a_materialNames)
	{
		const auto candidate = NormalizeMaterialName(a_rootMaterialName);
		return !candidate.empty()
			&& std::ranges::any_of(
				a_materialNames,
				[&candidate](const std::string& a_name) {
					return candidate == a_name;
				});
	}

	inline DrawClassification Classify(
		const ClassificationInput& a_input,
		std::span<const std::string> a_materialNames)
	{
		if (!a_input.alphaBlended)
			return {};

		if (a_input.hasExtraData) {
			if (!a_input.extraDataIsInteger) {
				return {
					.descriptor = kDescriptorDisabled,
					.source = ClassificationSource::kExtraData,
					.outcome = ClassificationOutcome::kExtraDataInvalid
				};
			}

			std::uint32_t descriptor =
				static_cast<std::uint32_t>(a_input.extraDataValue)
				& kMaterialMask;
			if (descriptor == 0)
				descriptor = kDescriptorDisabled;
			return {
				.descriptor = descriptor,
				.source = ClassificationSource::kExtraData,
				.outcome = descriptor >= 1 && descriptor <= 3 ?
					ClassificationOutcome::kExtraDataActive :
					ClassificationOutcome::kExtraDataDisabled
			};
		}

		if (a_input.fallbackEligible
			&& MatchesFallbackMaterial(
				a_input.rootMaterialName, a_materialNames)) {
			return {
				.descriptor = kDescriptorUseDefault,
				.source = ClassificationSource::kMaterialName,
				.outcome = ClassificationOutcome::kMaterialNameActive
			};
		}

		return {
			.descriptor = kDescriptorDisabled,
			.source = ClassificationSource::kNone,
			.outcome = ClassificationOutcome::kMaterialNameMiss
		};
	}

	constexpr std::uint32_t PackMode(
		MaterialModel a_defaultModel,
		const DrawClassification& a_classification,
		bool a_debug) noexcept
	{
		return (
				   static_cast<std::uint32_t>(a_defaultModel) & kMaterialMask)
			| ((a_classification.descriptor & kMaterialMask)
			   << kDescriptorShift)
			| ((static_cast<std::uint32_t>(a_classification.source) & 3U)
			   << kSourceShift)
			| (a_debug ? kDebugFlag : 0U);
	}

	constexpr std::uint32_t DefaultMaterial(std::uint32_t a_mode) noexcept
	{
		return a_mode & kMaterialMask;
	}

	constexpr std::uint32_t Descriptor(std::uint32_t a_mode) noexcept
	{
		return (a_mode >> kDescriptorShift) & kMaterialMask;
	}

	constexpr ClassificationSource Source(std::uint32_t a_mode) noexcept
	{
		return static_cast<ClassificationSource>((a_mode >> kSourceShift) & 3U);
	}

	constexpr bool DebugEnabled(std::uint32_t a_mode) noexcept
	{
		return (a_mode & kDebugFlag) != 0;
	}

	inline Settings Clamp(Settings a_settings)
	{
		const auto model = static_cast<std::uint32_t>(a_settings.materialModel);
		if (model > static_cast<std::uint32_t>(
						MaterialModel::kAnisotropicFabric)) {
			a_settings.materialModel = MaterialModel::kDisabled;
		}
		a_settings.alphaReduction =
			std::clamp(a_settings.alphaReduction, 0.0f, 1.0f);
		a_settings.alphaSoftness =
			std::clamp(a_settings.alphaSoftness, 0.0f, 1.0f);
		a_settings.alphaStrength =
			std::clamp(a_settings.alphaStrength, 0.0f, 1.0f);
		for (auto& name : a_settings.fallbackMaterialNames)
			name = NormalizeMaterialName(name);
		std::erase_if(
			a_settings.fallbackMaterialNames,
			[](const std::string& a_name) { return a_name.empty(); });
		std::ranges::sort(a_settings.fallbackMaterialNames);
		const auto unique = std::ranges::unique(
			a_settings.fallbackMaterialNames);
		a_settings.fallbackMaterialNames.erase(
			unique.begin(), unique.end());
		return a_settings;
	}

	constexpr float Saturate(float a_value) noexcept
	{
		return a_value < 0.0f ? 0.0f : (a_value > 1.0f ? 1.0f : a_value);
	}

	inline float SoftClamp(float a_alpha, float a_limit) noexcept
	{
		a_alpha = std::min(
			a_alpha,
			a_limit
				/ (1.0f
				   + std::exp(
					   -4.0f * (a_alpha - a_limit * 0.5f) / a_limit)));
		return Saturate(a_alpha);
	}

	inline float Dot(
		const std::array<float, 3>& a_left,
		const std::array<float, 3>& a_right) noexcept
	{
		return a_left[0] * a_right[0]
			+ a_left[1] * a_right[1]
			+ a_left[2] * a_right[2];
	}

	inline std::array<float, 3> Cross(
		const std::array<float, 3>& a_left,
		const std::array<float, 3>& a_right) noexcept
	{
		return {
			a_left[1] * a_right[2] - a_left[2] * a_right[1],
			a_left[2] * a_right[0] - a_left[0] * a_right[2],
			a_left[0] * a_right[1] - a_left[1] * a_right[0]
		};
	}

	inline float Length(const std::array<float, 3>& a_value) noexcept
	{
		return std::sqrt(Dot(a_value, a_value));
	}

	inline float ViewDependentAlphaNaive(
		float a_alpha,
		const std::array<float, 3>& a_view,
		const std::array<float, 3>& a_normal) noexcept
	{
		return 1.0f - (1.0f - a_alpha) * Dot(a_view, a_normal);
	}

	inline float ViewDependentAlphaFabric1D(
		float a_alpha,
		const std::array<float, 3>& a_view,
		const std::array<float, 3>& a_normal) noexcept
	{
		return a_alpha
			/ std::min(1.0f, std::abs(Dot(a_view, a_normal)) + 0.001f);
	}

	inline float ViewDependentAlphaFabric2D(
		float a_alpha,
		const std::array<float, 3>& a_view,
		const TangentBasis& a_tangentBasis) noexcept
	{
		const auto& tangent = a_tangentBasis[0];
		const auto& bitangent = a_tangentBasis[1];
		const auto& normal = a_tangentBasis[2];
		const float alpha0 = 1.0f - std::sqrt(1.0f - a_alpha);
		return alpha0
				* (Length(Cross(a_view, tangent))
				   + Length(Cross(a_view, bitangent)))
				/ (std::abs(Dot(a_view, normal)) + 0.001f)
			- alpha0 * alpha0;
	}

	inline float ApplyAlpha(
		float a_alpha,
		const std::array<float, 3>& a_view,
		const std::array<float, 3>& a_normal,
		const TangentBasis& a_tangentBasis,
		const Settings& a_settings,
		const DrawClassification& a_classification) noexcept
	{
		if (a_classification.source == ClassificationSource::kNone
			|| a_alpha < kMinimumAlpha
			|| a_alpha >= 1.0f) {
			return a_alpha;
		}

		auto material = a_classification.descriptor;
		float reduction = 0.0f;
		float softness = 0.0f;
		float strength = 0.0f;
		if (material == kDescriptorUseDefault) {
			material = static_cast<std::uint32_t>(a_settings.materialModel);
			reduction = a_settings.alphaReduction;
			softness = a_settings.alphaSoftness;
			strength = a_settings.alphaStrength;
		}
		if (material < 1
			|| material > static_cast<std::uint32_t>(
				MaterialModel::kAnisotropicFabric)) {
			return a_alpha;
		}

		const float originalAlpha = a_alpha;
		a_alpha *= 1.0f - Saturate(reduction);
		switch (static_cast<MaterialModel>(material)) {
		case MaterialModel::kAnisotropicFabric:
			a_alpha = ViewDependentAlphaFabric2D(
				a_alpha, a_view, a_tangentBasis);
			break;
		case MaterialModel::kIsotropicFabric:
			a_alpha = ViewDependentAlphaFabric1D(
				a_alpha, a_view, a_normal);
			break;
		case MaterialModel::kRimLight:
			a_alpha = ViewDependentAlphaNaive(
				a_alpha, a_view, a_normal);
			break;
		default:
			return originalAlpha;
		}
		a_alpha = SoftClamp(a_alpha, 2.0f - Saturate(softness));
		return a_alpha + (originalAlpha - a_alpha) * Saturate(strength);
	}
}
