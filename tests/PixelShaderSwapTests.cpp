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
				OwnShaderVariantKey(
					shader_variants::kBsdfCompositeAmbientIbl),
				stock
			},
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(
					shader_variants::
						kBsdfCompositeAmbientIblTilelight),
				stock
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			shader_variants::kBsdfCompositeAmbientIblTilelight,
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
				OwnShaderVariantKey(
					shader_variants::kBsdfCompositeAmbientIbl),
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
			shader_variants::kBsdfCompositeAmbientIbl,
			noTilelight);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kHashMismatch
				&& selection.variantIndex == 0,
			"variant hash mismatch did not refuse hash fallback");
	}

	void TestCrossSubclassKeyCollisionStaysGuarded()
	{
		using namespace cs::engine;
		const ShaderVariantKeyView composite{
			"BSDFCompositeShader",
			ShaderStage::kPixel,
			ShaderVariantId{ 0x00100048 }
		};
		const ShaderVariantKeyView light{
			"BSDFLightShader",
			ShaderStage::kPixel,
			ShaderVariantId{ 0x00100048 }
		};
		const auto compositeHash = Sha(
			"3c1355737e77d36cdbc37d6b76015b8eb2a15b53");
		const auto lightHash = Sha(
			"9969e800683c8a7c8afc25f41582415d79cbe47e");
		const std::vector variants{
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(composite),
				compositeHash
			},
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(light),
				lightHash
			}
		};

		const auto lightSelection = SelectPixelShaderSwapVariant(
			variants, light, lightHash);
		Check(
			lightSelection.kind
					== PixelShaderSwapSelectionKind::kSelected
				&& lightSelection.variantIndex == 1,
			"cross-subclass archive key selected the wrong row");

		const auto mismatch = SelectPixelShaderSwapVariant(
			variants, composite, lightHash);
		Check(
			mismatch.kind
					== PixelShaderSwapSelectionKind::kHashMismatch
				&& mismatch.variantIndex == 0,
			"cross-block key collision bypassed the stock SHA1 guard");
	}

	void TestHashlessVariantRefused()
	{
		using namespace cs::engine;
		const std::vector variants{
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(
					shader_variants::kBsdfCompositeAmbientIbl)
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			shader_variants::kBsdfCompositeAmbientIbl,
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
				OwnShaderVariantKey(
					shader_variants::
						kBsdfCompositeAmbientIblTilelight),
				tilelight
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			shader_variants::kBsdfCompositeAmbientIbl,
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
				OwnShaderVariantKey(
					shader_variants::
						kBsdfCompositeAmbientIblTilelight),
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

	void TestVariantKeyScopeIncludesStage()
	{
		using namespace cs::engine;
		const ShaderVariantKeyView pixel{
			"BSDFCompositeShader",
			ShaderStage::kPixel,
			ShaderVariantId{ 0 }
		};
		const ShaderVariantKeyView vertex{
			"BSDFCompositeShader",
			ShaderStage::kVertex,
			ShaderVariantId{ 0 }
		};
		const ShaderVariantKeyView otherSubclass{
			"BSDFLightShader",
			ShaderStage::kPixel,
			ShaderVariantId{ 0 }
		};
		Check(
			!ShaderVariantKeysConflict(pixel, vertex),
			"different shader stages collided");
		Check(
			!ShaderVariantKeysConflict(pixel, otherSubclass),
			"different shader subclasses collided");
		Check(
			ShaderVariantKeysConflict(pixel, pixel),
			"identical scoped keys did not conflict");

		const auto stock = Sha(0x51);
		const std::vector variants{
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(vertex),
				stock,
				7
			},
			PixelShaderSwapVariantKey{
				std::nullopt,
				stock,
				7
			}
		};
		const auto selection = SelectPixelShaderSwapVariant(
			variants, pixel, stock);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kSelected
				&& selection.variantIndex == 1
				&& selection.usedHashFallback,
			"vertex route blocked pixel hash fallback");
	}

	void TestCompositeResolutionStaysUnavailable()
	{
		using namespace cs::engine;
		Check(
			!ResolvePixelShaderVariant(
				"BSDFCompositeShader", 0xB60, std::nullopt)
				&& !ResolvePixelShaderVariant(
					"BSDFCompositeShader", 0x10B60, std::nullopt),
			"unresolved Tilelight state produced a variant key");
	}

	void TestCompositeResolverMasksAndForcesTilelight()
	{
		using namespace cs::engine;
		const auto noTilelight = ResolvePixelShaderVariant(
			"BSDFCompositeShader", 0x10B60, false);
		const auto tilelight = ResolvePixelShaderVariant(
			"BSDFCompositeShader", 0xB60, true);
		Check(
			noTilelight
				&& *noTilelight
					== shader_variants::kBsdfCompositeAmbientIbl,
			"Composite resolver did not force Tilelight off");
		Check(
			tilelight
				&& *tilelight
					== shader_variants::
						kBsdfCompositeAmbientIblTilelight,
			"Composite resolver did not force Tilelight on");

		for (const std::uint32_t discardedBit :
			{ 0x2u, 0x4u, 0x10u, 0x400u, 0x416u }) {
			const auto collapsed = ResolvePixelShaderVariant(
				"BSDFCompositeShader",
				0xB60 | discardedBit,
				false);
			Check(
				collapsed && noTilelight
					&& *collapsed == *noTilelight,
				"Composite resolver did not collapse discarded technique bits");
			Check(
				collapsed && noTilelight
					&& ShaderVariantKeysConflict(
						*collapsed,
						*noTilelight),
				"collapsed Composite PSIDs did not conflict");
		}

		const auto unknownBits = ResolvePixelShaderVariant(
			"BSDFCompositeShader", 0x00100048, false);
		Check(
			unknownBits
				&& unknownBits->id.Value() == 0x00100048,
			"Composite resolver discarded an opaque technique bit");
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
		{ "cross-subclass key collision guarded", &TestCrossSubclassKeyCollisionStaysGuarded },
		{ "hashless variant refused", &TestHashlessVariantRefused },
		{ "unmapped variant remains stock", &TestUnmappedVariantRemainsStock },
		{ "unavailable resolution falls back", &TestUnavailableResolutionFallsBackToHash },
		{ "variant key scope includes stage", &TestVariantKeyScopeIncludesStage },
		{ "composite unresolved state unavailable", &TestCompositeResolutionStaysUnavailable },
		{ "composite resolver masks technique", &TestCompositeResolverMasksAndForcesTilelight },
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
