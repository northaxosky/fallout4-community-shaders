#include "ExtendedTranslucencyMath.h"
#include "FeatureBuffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
	int failures = 0;

#define CHECK(condition)                                                        \
	do {                                                                        \
		if (!(condition)) {                                                      \
			std::cerr << "FAIL: " #condition " at line " << __LINE__ << '\n';   \
			++failures;                                                          \
		}                                                                       \
	} while (false)

	using namespace cs::features::extended_translucency;

	bool Near(float a_left, float a_right, float a_epsilon = 1.0e-5f)
	{
		return std::abs(a_left - a_right) <= a_epsilon;
	}

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream) {
			std::cerr << "FAIL: cannot open " << a_path.string() << '\n';
			++failures;
			return {};
		}
		return {
			std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>()
		};
	}

	std::size_t Count(
		std::string_view a_text,
		std::string_view a_needle)
	{
		std::size_t count = 0;
		std::size_t offset = 0;
		while ((offset = a_text.find(a_needle, offset))
			!= std::string_view::npos) {
			++count;
			offset += a_needle.size();
		}
		return count;
	}

	void TestDefaultsAndMaterialSurvey()
	{
		const Settings defaults;
		CHECK(defaults.materialModel == MaterialModel::kAnisotropicFabric);
		CHECK(Near(defaults.alphaReduction, 0.15f));
		CHECK(Near(defaults.alphaSoftness, 0.0f));
		CHECK(Near(defaults.alphaStrength, 0.0f));
		CHECK(defaults.fallbackMaterialNames.size() == 5);

		const auto classify = [&defaults](
			std::string_view a_material,
			bool a_fallbackEligible = true) {
			return Classify(
				{
					.alphaBlended = true,
					.fallbackEligible = a_fallbackEligible,
					.rootMaterialName = a_material
				},
				defaults.fallbackMaterialNames);
		};
		CHECK(
			classify("materials/Architecture/DiamondCity/"
					 "DiamondCloth02Alpha.BGSM")
				.outcome
			== ClassificationOutcome::kMaterialNameActive);
		CHECK(
			classify("materials\\SetDressing\\Minutemen\\"
					 "FlagMinutemen01Backlit.BGSM")
				.outcome
			== ClassificationOutcome::kMaterialNameActive);
		CHECK(
			classify("materials\\SetDressing\\PrewarFlag01.BGSM").outcome
			== ClassificationOutcome::kMaterialNameActive);
		CHECK(
			classify("materials\\Props\\BinPlastic.BGSM").outcome
			== ClassificationOutcome::kMaterialNameMiss);
		CHECK(classify("").outcome == ClassificationOutcome::kMaterialNameMiss);
		CHECK(
			classify("DiamondMetalIbeam01Alpha.BGSM").outcome
			== ClassificationOutcome::kMaterialNameMiss);

		auto materialsWithDecal = defaults.fallbackMaterialNames;
		materialsWithDecal.emplace_back(
			"DiamondClothPlain01AlphaDecal.BGSM");
		CHECK(
			Classify(
				{
					.alphaBlended = true,
					.fallbackEligible = false,
					.rootMaterialName =
						"DiamondClothPlain01AlphaDecal.BGSM"
				},
				materialsWithDecal)
				.outcome
			== ClassificationOutcome::kMaterialNameMiss);
	}

	void TestClassificationPrecedence()
	{
		const auto materials = DefaultFallbackMaterialNames();
		const auto active = Classify(
			{
				.alphaBlended = true,
				.hasExtraData = true,
				.extraDataIsInteger = true,
				.extraDataValue = 3,
				.rootMaterialName = "DiamondCloth02Alpha.BGSM"
			},
			materials);
		CHECK(active.outcome == ClassificationOutcome::kExtraDataActive);
		CHECK(active.source == ClassificationSource::kExtraData);
		CHECK(active.descriptor == 3);

		const auto disabled = Classify(
			{
				.alphaBlended = true,
				.hasExtraData = true,
				.extraDataIsInteger = true,
				.extraDataValue = 0,
				.rootMaterialName = "DiamondCloth02Alpha.BGSM"
			},
			materials);
		CHECK(disabled.outcome == ClassificationOutcome::kExtraDataDisabled);
		CHECK(disabled.source == ClassificationSource::kExtraData);
		CHECK(disabled.descriptor == kDescriptorDisabled);

		const auto invalid = Classify(
			{
				.alphaBlended = true,
				.hasExtraData = true,
				.extraDataIsInteger = false,
				.rootMaterialName = "DiamondCloth02Alpha.BGSM"
			},
			materials);
		CHECK(invalid.outcome == ClassificationOutcome::kExtraDataInvalid);
		CHECK(invalid.source == ClassificationSource::kExtraData);

		const auto masked = Classify(
			{
				.alphaBlended = true,
				.hasExtraData = true,
				.extraDataIsInteger = true,
				.extraDataValue = 11
			},
			materials);
		CHECK(masked.outcome == ClassificationOutcome::kExtraDataActive);
		CHECK(masked.descriptor == 3);

		const auto reserved = Classify(
			{
				.alphaBlended = true,
				.hasExtraData = true,
				.extraDataIsInteger = true,
				.extraDataValue = 4
			},
			materials);
		CHECK(reserved.outcome == ClassificationOutcome::kExtraDataDisabled);
		CHECK(reserved.descriptor == 4);

		const auto opaque = Classify(
			{
				.alphaBlended = false,
				.hasExtraData = true,
				.extraDataIsInteger = true,
				.extraDataValue = 3
			},
			materials);
		CHECK(opaque.outcome == ClassificationOutcome::kNotAlphaBlended);
		CHECK(opaque.source == ClassificationSource::kNone);
	}

	void TestPacking()
	{
		const DrawClassification classification{
			.descriptor = 2,
			.source = ClassificationSource::kExtraData,
			.outcome = ClassificationOutcome::kExtraDataActive
		};
		const auto mode = PackMode(
			MaterialModel::kAnisotropicFabric,
			classification,
			true);
		CHECK(DefaultMaterial(mode) == 3);
		CHECK(Descriptor(mode) == 2);
		CHECK(Source(mode) == ClassificationSource::kExtraData);
		CHECK(DebugEnabled(mode));
	}

	void TestAlphaMath()
	{
		constexpr std::array view{ 0.0f, 0.6f, 0.8f };
		constexpr std::array normal{ 0.0f, 0.0f, 1.0f };
		constexpr std::array tangent{ 1.0f, 0.0f, 0.0f };
		constexpr std::array bitangent{ 0.0f, 1.0f, 0.0f };
		Settings settings;
		const DrawClassification fallback{
			.descriptor = kDescriptorUseDefault,
			.source = ClassificationSource::kMaterialName,
			.outcome = ClassificationOutcome::kMaterialNameActive
		};
		const DrawClassification explicitModel{
			.descriptor =
				static_cast<std::uint32_t>(
					MaterialModel::kAnisotropicFabric),
			.source = ClassificationSource::kExtraData,
			.outcome = ClassificationOutcome::kExtraDataActive
		};

		const float fallbackAlpha = ApplyAlpha(
			0.5f,
			view,
			normal,
			tangent,
			bitangent,
			settings,
			fallback);
		Settings neutral = settings;
		neutral.alphaReduction = 0.0f;
		neutral.alphaSoftness = 0.0f;
		neutral.alphaStrength = 0.0f;
		const float explicitAlpha = ApplyAlpha(
			0.5f,
			view,
			normal,
			tangent,
			bitangent,
			settings,
			explicitModel);
		const float explicitNeutralAlpha = ApplyAlpha(
			0.5f,
			view,
			normal,
			tangent,
			bitangent,
			neutral,
			explicitModel);
		CHECK(!Near(fallbackAlpha, explicitAlpha));
		CHECK(Near(explicitAlpha, explicitNeutralAlpha));

		settings.alphaStrength = 1.0f;
		CHECK(Near(
			ApplyAlpha(
				0.5f,
				view,
				normal,
				tangent,
				bitangent,
				settings,
				fallback),
			0.5f));
		CHECK(Near(
			ApplyAlpha(
				kMinimumAlpha * 0.5f,
				view,
				normal,
				tangent,
				bitangent,
				settings,
				fallback),
			kMinimumAlpha * 0.5f));
	}

	void TestFeatureBufferPatch()
	{
		using cs::ExtendedTranslucencyFeatureData;
		using cs::FeatureDataCB;
		static_assert(sizeof(FeatureDataCB) == 144);
		static_assert(
			offsetof(FeatureDataCB, extendedTranslucencySettings) == 128);
		static_assert(sizeof(ExtendedTranslucencyFeatureData) == 16);

		FeatureDataCB source{};
		auto* sourceBytes = reinterpret_cast<std::byte*>(&source);
		for (std::size_t index = 0; index < sizeof(source); ++index)
			sourceBytes[index] = static_cast<std::byte>(index);
		const ExtendedTranslucencyFeatureData patch{
			.PackedMode = 0x12345678U,
			.AlphaReduction = 0.25f,
			.AlphaSoftness = 0.5f,
			.AlphaStrength = 0.75f
		};
		const auto result =
			cs::PatchExtendedTranslucencyFeatureData(source, patch);
		const auto* resultBytes =
			reinterpret_cast<const std::byte*>(&result);
		for (std::size_t index = 0; index < sizeof(result); ++index) {
			if (index < 128 || index >= 128 + sizeof(patch))
				CHECK(resultBytes[index] == sourceBytes[index]);
		}
		CHECK(
			std::memcmp(
				resultBytes + 128,
				&patch,
				sizeof(patch))
			== 0);
	}

	void TestShaderContracts(
		const std::filesystem::path& a_hlsli,
		const std::filesystem::path& a_lighting,
		const std::filesystem::path& a_shared)
	{
		const auto hlsli = ReadFile(a_hlsli);
		const auto lighting = ReadFile(a_lighting);
		const auto shared = ReadFile(a_shared);
		CHECK(hlsli.contains("float4 ApplyToColor("));
		CHECK(
			hlsli.contains(
				"if (material == MaterialDefault)"));
		CHECK(
			Count(
				lighting,
				"ExtendedTranslucency::ApplyToColor(")
			== 3);
		CHECK(
			shared.find("ExtendedTranslucencySettings "
						"extendedTranslucencySettings;")
			> shared.find("WaterEffectsSettings waterEffectsSettings;"));
	}
}

int main(int a_argc, char* a_argv[])
{
	if (a_argc != 4) {
		std::cerr
			<< "usage: ExtendedTranslucencyMathTests <hlsli> <lighting> "
			   "<shared-data>\n";
		return 2;
	}
	TestDefaultsAndMaterialSurvey();
	TestClassificationPrecedence();
	TestPacking();
	TestAlphaMath();
	TestFeatureBufferPatch();
	TestShaderContracts(a_argv[1], a_argv[2], a_argv[3]);

	if (failures == 0)
		std::cout << "Extended translucency math and contracts passed\n";
	return failures == 0 ? 0 : 1;
}
