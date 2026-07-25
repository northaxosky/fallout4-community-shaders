#include "Render/PixelShaderSwapBroker.h"

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

	void TestTechniqueSelectsVariant()
	{
		using namespace cs::engine;
		const auto stock = Sha(0x31);
		const std::vector variants{
			PixelShaderSwapVariantKey{
				PixelShaderTechnique{ "BSDFCompositeShader", 0xB60 },
				stock
			},
			PixelShaderSwapVariantKey{
				PixelShaderTechnique{ "BSDFCompositeShader", 0x10B60 },
				stock
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			PixelShaderTechniqueView{ "BSDFCompositeShader", 0x10B60 },
			stock);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kSelected
				&& selection.variantIndex == 1,
			"technique key did not select the matching variant");
	}

	void TestTechniqueHashMismatchRefused()
	{
		using namespace cs::engine;
		const std::vector variants{
			PixelShaderSwapVariantKey{
				PixelShaderTechnique{ "BSDFCompositeShader", 0x10B60 },
				Sha(0x31)
			},
			PixelShaderSwapVariantKey{
				std::nullopt,
				Sha(0x62)
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			PixelShaderTechniqueView{ "BSDFCompositeShader", 0x10B60 },
			Sha(0x62));
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kHashMismatch
				&& selection.variantIndex == 0,
			"technique hash mismatch did not refuse hash fallback");
	}

	void TestUnavailableAttributionFallsBackToHash()
	{
		using namespace cs::engine;
		const std::vector variants{
			PixelShaderSwapVariantKey{
				PixelShaderTechnique{ "BSDFCompositeShader", 0x10B60 },
				Sha(0x31)
			},
			PixelShaderSwapVariantKey{
				std::nullopt,
				Sha(0x62)
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants, std::nullopt, Sha(0x62));
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kSelected
				&& selection.variantIndex == 1
				&& selection.usedHashFallback,
			"missing attribution did not fall back to exact hash");
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
		{ "technique selects variant", &TestTechniqueSelectsVariant },
		{ "technique hash mismatch refused", &TestTechniqueHashMismatchRefused },
		{ "unavailable attribution falls back", &TestUnavailableAttributionFallsBackToHash }
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
