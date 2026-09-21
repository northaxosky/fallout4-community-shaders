#include "Render/DeferredDrawAnchor.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderVariantResolver.h"

#include <array>
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

	void TestDeferredDrawAnchorTruthTable()
	{
		using cs::engine::DeferredDrawAnchorDecision;
		using cs::engine::SelectDeferredDrawAnchorDecision;

		struct TestCase
		{
			bool insideLights;
			bool insideComposite;
			std::uint32_t residualR9d;
			DeferredDrawAnchorDecision expected;
		};

		constexpr std::array cases{
			TestCase{ false, false, 0, {} },
			TestCase{ false, false, 2, {} },
			TestCase{ true, false, 0, {} },
			TestCase{ true, false, 2, { true, true } },
			TestCase{ false, true, 0, { true, false } },
			TestCase{ false, true, 2, { true, false } },
			TestCase{ true, true, 2, { true, false } }
		};
		for (const auto& testCase : cases) {
			Check(
				SelectDeferredDrawAnchorDecision(
					testCase.insideLights,
					testCase.insideComposite,
					testCase.residualR9d)
					== testCase.expected,
				"deferred draw anchor decision truth table mismatch");
		}
		Check(
			SelectDeferredDrawAnchorDecision(true, false, UINT32_MAX)
				== DeferredDrawAnchorDecision{},
			"lights phase accepted an unrelated residual r9d value");
	}

	struct PipelineFixture
	{
		ID3D11Device* expectedDevice = nullptr;
		ID3D11ClassLinkage* expectedLinkage = nullptr;
		ID3D11DeviceChild* stock = nullptr;
		ID3D11DeviceChild* replacement = nullptr;
		cs::engine::ShaderStage expectedStage =
			cs::engine::ShaderStage::kPixel;
		HRESULT originalResult = S_OK;
		std::vector<int> order;
		bool resolverForwarded = false;
		cs::engine::ShaderSwapResolverResult firstResolverResult =
			cs::engine::ShaderSwapResolverResult::kNoMatch;
		bool lowerResolverCalled = false;
	};

	PipelineFixture* g_pipelineFixture = nullptr;

	HRESULT STDMETHODCALLTYPE PipelineOriginal(
		ID3D11Device* a_device,
		const void* a_bytecode,
		SIZE_T a_bytecodeLength,
		ID3D11ClassLinkage* a_linkage,
		ID3D11DeviceChild** a_output)
	{
		auto& fixture = *g_pipelineFixture;
		fixture.order.push_back(2);
		Check(
			a_device == fixture.expectedDevice
				&& a_linkage == fixture.expectedLinkage
				&& a_bytecode
				&& a_bytecodeLength != 0,
			"original CreatePS did not receive broker inputs");
		if (a_output)
			*a_output = fixture.stock;
		return fixture.originalResult;
	}

	cs::engine::ShaderSwapResolverResult PipelineResolver(
		const cs::engine::ShaderSwapRequest& a_request) noexcept
	{
		auto& fixture = *g_pipelineFixture;
		fixture.order.push_back(4);
		fixture.resolverForwarded =
			a_request.device == fixture.expectedDevice
			&& a_request.linkage == fixture.expectedLinkage
			&& a_request.stockOutput == fixture.stock
			&& a_request.output
			&& *a_request.output == fixture.stock
			&& a_request.stage == fixture.expectedStage;
		if (a_request.output)
			*a_request.output = fixture.replacement;
		return cs::engine::ShaderSwapResolverResult::kReplaced;
	}

	cs::engine::ShaderSwapResolverResult FirstPipelineResolver(
		const cs::engine::ShaderSwapRequest&) noexcept
	{
		return g_pipelineFixture->firstResolverResult;
	}

	cs::engine::ShaderSwapResolverResult LowerPipelineResolver(
		const cs::engine::ShaderSwapRequest& a_request) noexcept
	{
		g_pipelineFixture->lowerResolverCalled = true;
		if (a_request.output)
			*a_request.output = g_pipelineFixture->replacement;
		return cs::engine::ShaderSwapResolverResult::kReplaced;
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
		}

		const auto unknownBits = ResolvePixelShaderVariant(
			"BSDFCompositeShader", 0x00100048, false);
		Check(
			unknownBits
				&& unknownBits->id.Value() == 0x00100048,
			"Composite resolver discarded an opaque technique bit");
	}

	void TestBsdfLightResolverMasksTechnique()
	{
		using namespace cs::engine;
		const auto checkKnownKeys = [](const auto& a_keys) {
			for (const auto key : a_keys) {
				const auto resolved = ResolvePixelShaderVariant(
					key.subclass,
					key.id.Value(),
					std::nullopt);
				Check(
					resolved && *resolved == key,
					"Light resolver changed a shipped PSID");
			}
		};
		checkKnownKeys(shader_variants::kBsdfLightDeferredPoint);
		checkKnownKeys(
			shader_variants::kBsdfLightDeferredDirectional);
		checkKnownKeys(
			shader_variants::
				kBsdfLightDeferredDirectionalIbl);

		const auto setupAlias = ResolvePixelShaderVariant(
			"BSDFLightShader", 0x02001204, std::nullopt);
		Check(
			setupAlias
				&& setupAlias->id.Value() == 0x00001204,
			"Light fallback mask retained CPU-only setup bit");

		const auto keyFeature = ResolvePixelShaderVariant(
			"BSDFLightShader", 0x10000002, std::nullopt);
		Check(
			keyFeature
				&& keyFeature->id.Value() == 0x10000002,
			"Light key-feature branch was not identity");

		const auto overdraw = ResolvePixelShaderVariant(
			"BSDFLightShader", 0xFFFFFFFF, std::nullopt);
		Check(
			overdraw
				&& overdraw->id.Value() == 0xF801257F,
			"Light overdraw mask produced the wrong PSID");

		const auto stencil = ResolvePixelShaderVariant(
			"BSDFLightShader", 0x104, std::nullopt);
		Check(
			stencil && stencil->id.Value() == 0x104,
			"Light stencil technique resolved incorrectly");

		constexpr std::array normalizationCases{
			std::pair{ 0x00020080U, 0x00020000U },
			std::pair{ 0x04020080U, 0x04020000U },
			std::pair{ 0x00850202U, 0x00850202U },
			std::pair{ 0x000D0202U, 0x000D0202U }
		};
		for (const auto [raw, expected] : normalizationCases) {
			const auto resolved = ResolvePixelShaderVariant(
				"BSDFLightShader", raw, std::nullopt);
			Check(
				resolved && resolved->id.Value() == expected,
				"Light resolver normalized a technique incorrectly");
		}
	}

	void TestBrokerPipelineForwarding()
	{
		using namespace cs::engine;
		std::byte deviceToken{};
		std::byte linkageToken{};
		std::byte stockToken{};
		std::byte replacementToken{};
		PipelineFixture fixture{
			.expectedDevice =
				reinterpret_cast<ID3D11Device*>(&deviceToken),
			.expectedLinkage =
				reinterpret_cast<ID3D11ClassLinkage*>(&linkageToken),
			.stock =
				reinterpret_cast<ID3D11DeviceChild*>(&stockToken),
			.replacement =
				reinterpret_cast<ID3D11DeviceChild*>(&replacementToken)
		};
		g_pipelineFixture = &fixture;
		const std::array resolvers{
			PixelShaderSwapResolverRegistration{
				.resolver = &PipelineResolver
			}
		};
		const std::array<std::byte, 4> bytecode{
			std::byte{ 1 },
			std::byte{ 2 },
			std::byte{ 3 },
			std::byte{ 4 }
		};
		ID3D11DeviceChild* output = nullptr;
		const auto result = ExecuteShaderSwapPipeline(
			&PipelineOriginal,
			resolvers,
			ShaderVariantKeyView{
				"BSDFLightShader",
				ShaderStage::kPixel,
				ShaderVariantId{ 0x1234 }
			},
			false,
			ShaderStage::kPixel,
			fixture.expectedDevice,
			bytecode.data(),
			bytecode.size(),
			fixture.expectedLinkage,
			&output);
		Check(result == S_OK, "broker did not preserve original HRESULT");
		Check(
			output == fixture.replacement,
			"broker did not publish resolver replacement");
		Check(
			fixture.order == std::vector<int>{ 2, 4 },
			"broker resolver ordering changed");
		Check(
			fixture.resolverForwarded,
			"broker did not forward stock/device/linkage to resolver");

		fixture.order.clear();
		output = nullptr;
		{
			ScopedPixelShaderBrokerBypass bypass;
			Check(
				ExecuteShaderSwapPipeline(
					&PipelineOriginal,
					resolvers,
					std::nullopt,
					PixelShaderBrokerBypassActive(),
					ShaderStage::kPixel,
					fixture.expectedDevice,
					bytecode.data(),
					bytecode.size(),
					fixture.expectedLinkage,
					&output)
					== S_OK,
				"bypassed CreatePS changed original HRESULT");
		}
		Check(
			fixture.order == std::vector<int>{ 2 }
				&& output == fixture.stock,
			"broker bypass recursed into resolvers");

		fixture.order.clear();
		fixture.originalResult = E_FAIL;
		output = nullptr;
		Check(
			ExecuteShaderSwapPipeline(
				&PipelineOriginal,
				resolvers,
				std::nullopt,
				false,
				ShaderStage::kPixel,
				fixture.expectedDevice,
				bytecode.data(),
				bytecode.size(),
				fixture.expectedLinkage,
				&output)
				== E_FAIL,
			"failed stock CreatePS HRESULT was not preserved");
		Check(
			fixture.order == std::vector<int>{ 2 },
			"failed stock CreatePS invoked resolver");
	}

	void TestResolverClaimStopsLowerPriority()
	{
		using namespace cs::engine;
		std::byte deviceToken{};
		std::byte linkageToken{};
		std::byte stockToken{};
		std::byte replacementToken{};
		PipelineFixture fixture{
			.expectedDevice =
				reinterpret_cast<ID3D11Device*>(&deviceToken),
			.expectedLinkage =
				reinterpret_cast<ID3D11ClassLinkage*>(&linkageToken),
			.stock =
				reinterpret_cast<ID3D11DeviceChild*>(&stockToken),
			.replacement =
				reinterpret_cast<ID3D11DeviceChild*>(&replacementToken),
			.firstResolverResult =
				ShaderSwapResolverResult::kKeepStock
		};
		g_pipelineFixture = &fixture;
		const std::array resolvers{
			PixelShaderSwapResolverRegistration{
				.resolver = &FirstPipelineResolver,
				.priority = kEarlyResolverPriority
			},
			PixelShaderSwapResolverRegistration{
				.resolver = &LowerPipelineResolver,
				.priority = kHlslReplacementResolverPriority
			}
		};
		const std::array<std::byte, 1> bytecode{ std::byte{ 1 } };
		ID3D11DeviceChild* output = nullptr;
		Check(
			ExecuteShaderSwapPipeline(
				&PipelineOriginal,
				resolvers,
				std::nullopt,
				false,
				ShaderStage::kPixel,
				fixture.expectedDevice,
				bytecode.data(),
				bytecode.size(),
				fixture.expectedLinkage,
				&output)
				== S_OK
				&& output == fixture.stock
				&& !fixture.lowerResolverCalled,
			"claimed stock route fell through to lower HLSL resolver");

		fixture.firstResolverResult =
			ShaderSwapResolverResult::kNoMatch;
		fixture.lowerResolverCalled = false;
		output = nullptr;
		Check(
			ExecuteShaderSwapPipeline(
				&PipelineOriginal,
				resolvers,
				std::nullopt,
				false,
				ShaderStage::kPixel,
				fixture.expectedDevice,
				bytecode.data(),
				bytecode.size(),
				fixture.expectedLinkage,
				&output)
				== S_OK
				&& output == fixture.replacement
				&& fixture.lowerResolverCalled,
			"unmatched patch route did not reach lower HLSL resolver");
	}

	void TestResolverStageMask()
	{
		using namespace cs::engine;
		std::byte deviceToken{};
		std::byte linkageToken{};
		std::byte stockToken{};
		std::byte replacementToken{};
		PipelineFixture fixture{
			.expectedDevice =
				reinterpret_cast<ID3D11Device*>(&deviceToken),
			.expectedLinkage =
				reinterpret_cast<ID3D11ClassLinkage*>(&linkageToken),
			.stock =
				reinterpret_cast<ID3D11DeviceChild*>(&stockToken),
			.replacement =
				reinterpret_cast<ID3D11DeviceChild*>(&replacementToken),
			.expectedStage = ShaderStage::kVertex
		};
		g_pipelineFixture = &fixture;
		const std::array resolvers{
			PixelShaderSwapResolverRegistration{
				.resolver = &PipelineResolver,
				.stages = ShaderStageBit(ShaderStage::kPixel)
			}
		};
		const std::array<std::byte, 1> bytecode{ std::byte{ 1 } };
		ID3D11DeviceChild* output = nullptr;
		Check(
			ExecuteShaderSwapPipeline(
				&PipelineOriginal,
				resolvers,
				std::nullopt,
				false,
				ShaderStage::kVertex,
				fixture.expectedDevice,
				bytecode.data(),
				bytecode.size(),
				fixture.expectedLinkage,
				&output)
				== S_OK,
			"vertex pipeline changed the original result");
		Check(
			fixture.order == std::vector<int>{ 2 }
				&& output == fixture.stock
				&& !fixture.resolverForwarded,
			"pixel resolver ran for a vertex shader");
	}

	void TestResolverRegistryGeneration()
	{
		using namespace cs::engine;
		PixelShaderResolverRegistryModel registry;
		Check(
			registry.Generation() == 0
				&& registry.Identities().empty(),
			"resolver registry did not start empty");
		const auto first = registry.Register(-100);
		const auto second = registry.Register(0);
		Check(
			first == 1
				&& second == 2
				&& registry.Generation() == 2
				&& registry.Identities().size() == 2,
			"resolver registration generation is wrong");
		Check(
			BuildPixelShaderResolverRegistryDescriptor(
				registry.Identities())
				== "{\"resolvers\":[{\"priority\":-100,"
				   "\"registration_generation\":1},{\"priority\":0,"
				   "\"registration_generation\":2}],"
				   "\"schema\":\"fo4cs.broker-resolver-registry\","
				   "\"schema_version\":1}\n",
			"resolver registry descriptor is not canonical");
		Check(
			registry.Unregister(first)
				&& registry.Generation() == 3
				&& registry.Identities().size() == 1
				&& !registry.Unregister(first),
			"resolver unregistration generation is wrong");
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
		{ "deferred draw anchor truth table", &TestDeferredDrawAnchorTruthTable },
		{ "composite unresolved state unavailable", &TestCompositeResolutionStaysUnavailable },
		{ "composite resolver masks technique", &TestCompositeResolverMasksAndForcesTilelight },
		{ "Light resolver masks technique", &TestBsdfLightResolverMasksTechnique },
		{ "broker pipeline forwarding", &TestBrokerPipelineForwarding },
		{ "resolver claim stops lower priority", &TestResolverClaimStopsLowerPriority },
		{ "resolver stage mask", &TestResolverStageMask },
		{ "resolver registry generation", &TestResolverRegistryGeneration }
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
