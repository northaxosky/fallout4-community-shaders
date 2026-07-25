#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderVariantResolver.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	struct Failure : std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw Failure(std::string(a_message));
	}

	cs::sha1::Sha1Result Sha(std::uint8_t a_value)
	{
		cs::sha1::Sha1Result result{};
		result.bytes[0] = a_value;
		return result;
	}

	cs::sha1::Sha1Result Sha(std::string_view a_hex)
	{
		cs::sha1::Sha1Result result{};
		Check(
			cs::sha1::Sha1FromHex(std::string(a_hex), result),
			"invalid SHA1 fixture");
		return result;
	}

	void TestVariantKeySelectsVariant()
	{
		using namespace cs::engine;
		const auto stock = Sha(0x31);
		const std::vector variants{
			PixelShaderSwapVariantKey{
				PixelShaderVariant{
					"BSDFCompositeShader",
					shader_variants::kBsdfCompositeAmbientIbl
				},
				stock
			},
			PixelShaderSwapVariantKey{
				PixelShaderVariant{
					"BSDFCompositeShader",
					shader_variants::kBsdfCompositeAmbientIblTilelight
				},
				stock
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			PixelShaderVariantView{
				"BSDFCompositeShader",
				shader_variants::kBsdfCompositeAmbientIblTilelight
			},
			stock);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kSelected
				&& selection.variantIndex == 1,
			"variant key did not select the matching variant");
	}

	void TestVariantHashMismatchRefused()
	{
		using namespace cs::engine;
		const auto tilelight = Sha(
			"2b6e36c08aca7ff0a3bd10da326e00b3b0367383");
		const auto noTilelight = Sha(
			"6d726d0fe6b6c474da30edbffcecfa067c795873");
		const std::vector variants{
			PixelShaderSwapVariantKey{
				PixelShaderVariant{
					"BSDFCompositeShader",
					shader_variants::kBsdfCompositeAmbientIbl
				},
				tilelight
			},
			PixelShaderSwapVariantKey{
				std::nullopt,
				noTilelight,
				1
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			PixelShaderVariantView{
				"BSDFCompositeShader",
				shader_variants::kBsdfCompositeAmbientIbl
			},
			noTilelight);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kHashMismatch
				&& selection.variantIndex == 0,
			"variant hash mismatch did not refuse hash fallback");
	}

	void TestHashlessVariantRefused()
	{
		using namespace cs::engine;
		const std::vector variants{
			PixelShaderSwapVariantKey{
				PixelShaderVariant{
					"BSDFCompositeShader",
					shader_variants::kBsdfCompositeAmbientIbl
				}
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			PixelShaderVariantView{
				"BSDFCompositeShader",
				shader_variants::kBsdfCompositeAmbientIbl
			},
			Sha("6d726d0fe6b6c474da30edbffcecfa067c795873"));
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kHashMismatch,
			"hashless keyed variant bypassed the stock SHA1 guard");
	}

	void TestUnmappedVariantRemainsStock()
	{
		using namespace cs::engine;
		const auto tilelight = Sha(
			"2b6e36c08aca7ff0a3bd10da326e00b3b0367383");
		const auto noTilelight = Sha(
			"6d726d0fe6b6c474da30edbffcecfa067c795873");
		const std::vector variants{
			PixelShaderSwapVariantKey{
				PixelShaderVariant{
					"BSDFCompositeShader",
					shader_variants::kBsdfCompositeAmbientIblTilelight
				},
				tilelight
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			PixelShaderVariantView{
				"BSDFCompositeShader",
				shader_variants::kBsdfCompositeAmbientIbl
			},
			noTilelight);
		Check(
			selection.kind
				== PixelShaderSwapSelectionKind::kUnmappedVariant,
			"known unmapped variant did not remain stock");
	}

	void TestUnavailableResolutionFallsBackToHash()
	{
		using namespace cs::engine;
		const auto tilelight = Sha(
			"2b6e36c08aca7ff0a3bd10da326e00b3b0367383");
		const std::vector variants{
			PixelShaderSwapVariantKey{
				PixelShaderVariant{
					"BSDFCompositeShader",
					shader_variants::kBsdfCompositeAmbientIblTilelight
				},
				tilelight
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants, std::nullopt, tilelight);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kSelected
				&& selection.variantIndex == 0
				&& selection.usedHashFallback,
			"unresolved variant did not fall back to exact hash");
	}

	void TestCompositeResolutionStaysUnavailable()
	{
		using namespace cs::engine;
		Check(
			!ResolvePixelShaderVariant("BSDFCompositeShader", 0xB60)
				&& !ResolvePixelShaderVariant(
					"BSDFCompositeShader", 0x10B60),
			"unresolved Tilelight state produced a variant key");
	}

	void TestNotReadyReplacementKeepsStock()
	{
		using namespace cs::engine;
		Check(
			!ShouldSubstitutePixelShader(
				PixelShaderSwapSelectionKind::kSelected, false),
			"selected but not-ready replacement did not retain stock");
		Check(
			ShouldSubstitutePixelShader(
				PixelShaderSwapSelectionKind::kSelected, true),
			"ready selected replacement did not substitute");
		Check(
			!ShouldSubstitutePixelShader(
				PixelShaderSwapSelectionKind::kHashMismatch, true),
			"guard mismatch allowed substitution");
	}
}

int main()
{
	struct Test
	{
		const char* name;
		void (*run)();
	};
	const Test tests[]{
		{ "variant key selects variant", &TestVariantKeySelectsVariant },
		{ "variant hash mismatch refused", &TestVariantHashMismatchRefused },
		{ "hashless variant refused", &TestHashlessVariantRefused },
		{ "unmapped variant remains stock", &TestUnmappedVariantRemainsStock },
		{ "unavailable resolution falls back", &TestUnavailableResolutionFallsBackToHash },
		{ "composite resolution stays unavailable", &TestCompositeResolutionStaysUnavailable },
		{ "not-ready replacement keeps stock", &TestNotReadyReplacementKeepsStock }
	};

	unsigned failures = 0;
	for (const auto& test : tests) {
		try {
			test.run();
			std::cout << "PASS: " << test.name << '\n';
		} catch (const std::exception& e) {
			++failures;
			std::cerr << "FAIL: " << test.name << ": " << e.what() << '\n';
		}
	}
	return failures == 0 ? 0 : 1;
}
