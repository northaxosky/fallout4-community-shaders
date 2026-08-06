#include "Render/EngineCurrentPixelShader.h"
#include "Render/EnginePixelShaderIdentity.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderVariantResolver.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
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

	struct PipelineFixture
	{
		ID3D11Device* expectedDevice = nullptr;
		ID3D11ClassLinkage* expectedLinkage = nullptr;
		ID3D11PixelShader* stock = nullptr;
		ID3D11PixelShader* replacement = nullptr;
		HRESULT originalResult = S_OK;
		std::vector<int> order;
		cs::engine::PixelShaderSwapCompletion completion;
		bool resolverForwarded = false;
		cs::engine::PixelShaderSwapResolverResult firstResolverResult =
			cs::engine::PixelShaderSwapResolverResult::kNoMatch;
		bool lowerResolverCalled = false;
		bool mutateInput = false;
	};

	PipelineFixture* g_pipelineFixture = nullptr;

	struct DetailedObserverFixture
	{
		bool called = false;
		std::size_t bytecodeLength = 0;
		bool classLinkagePresent = false;
		std::string subclass;
		std::uint32_t rawTechnique = 0;
		std::optional<std::uint32_t> pluginResolvedPsid;
		std::optional<std::uint32_t> engineLookupPsid;
		std::optional<std::uint64_t> engineLookupSequence;
		std::optional<bool> tiledLighting;
	};

	DetailedObserverFixture* g_detailedObserverFixture = nullptr;

	HRESULT STDMETHODCALLTYPE PipelineOriginal(
		ID3D11Device* a_device,
		const void* a_bytecode,
		SIZE_T a_bytecodeLength,
		ID3D11ClassLinkage* a_linkage,
		ID3D11PixelShader** a_output)
	{
		auto& fixture = *g_pipelineFixture;
		fixture.order.push_back(2);
		Check(
			a_device == fixture.expectedDevice
				&& a_linkage == fixture.expectedLinkage,
			"original CreatePS did not receive broker device/linkage");
		if (fixture.mutateInput && a_bytecode && a_bytecodeLength != 0) {
			auto* bytes = const_cast<std::byte*>(
				static_cast<const std::byte*>(a_bytecode));
			bytes[0] ^= std::byte{ 0xff };
		}
		if (a_output)
			*a_output = fixture.stock;
		return fixture.originalResult;
	}

	void* PreparePipelineObserver(
		const void*,
		std::size_t) noexcept
	{
		g_pipelineFixture->order.push_back(1);
		return g_pipelineFixture;
	}

	void* PrepareDetailedObserver(
		const cs::engine::PixelShaderCreationDescriptor&
			a_descriptor) noexcept
	{
		auto& fixture = *g_detailedObserverFixture;
		fixture.called = true;
		fixture.bytecodeLength = a_descriptor.bytecodeLength;
		fixture.classLinkagePresent =
			a_descriptor.classLinkagePresent;
		if (a_descriptor.route) {
			fixture.subclass = a_descriptor.route->subclass;
			fixture.rawTechnique =
				a_descriptor.route->rawTechnique;
			if (a_descriptor.route->pluginResolvedPsid) {
				fixture.pluginResolvedPsid =
					a_descriptor.route
						->pluginResolvedPsid->Value();
			}
			if (a_descriptor.route->engineLookup) {
				fixture.engineLookupPsid =
					a_descriptor.route->engineLookup
						->returnedPsid.Value();
				fixture.engineLookupSequence =
					a_descriptor.route->engineLookup
						->callSequence;
			}
			fixture.tiledLighting =
				a_descriptor.route->tiledLighting;
		}
		return &fixture;
	}

	void ObservePipelineOriginal(
		void*,
		const cs::sha1::Sha1Result&,
		ID3D11PixelShader* a_shader) noexcept
	{
		g_pipelineFixture->order.push_back(3);
		g_pipelineFixture->resolverForwarded =
			g_pipelineFixture->resolverForwarded
			|| a_shader == g_pipelineFixture->stock;
	}

	void CompletePipelineObserver(
		void*,
		const cs::engine::PixelShaderSwapCompletion& a_completion) noexcept
	{
		g_pipelineFixture->order.push_back(5);
		g_pipelineFixture->completion = a_completion;
	}

	cs::engine::PixelShaderSwapResolverResult PipelineResolver(
		const cs::engine::PixelShaderSwapRequest& a_request) noexcept
	{
		auto& fixture = *g_pipelineFixture;
		fixture.order.push_back(4);
		fixture.resolverForwarded =
			fixture.resolverForwarded
			&& a_request.device == fixture.expectedDevice
			&& a_request.linkage == fixture.expectedLinkage
			&& a_request.stockOutput == fixture.stock
			&& a_request.output
			&& *a_request.output == fixture.stock;
		if (a_request.output)
			*a_request.output = fixture.replacement;
		return cs::engine::PixelShaderSwapResolverResult::kReplaced;
	}

	cs::engine::PixelShaderSwapResolverResult FirstPipelineResolver(
		const cs::engine::PixelShaderSwapRequest&) noexcept
	{
		return g_pipelineFixture->firstResolverResult;
	}

	cs::engine::PixelShaderSwapResolverResult LowerPipelineResolver(
		const cs::engine::PixelShaderSwapRequest& a_request) noexcept
	{
		g_pipelineFixture->lowerResolverCalled = true;
		if (a_request.output)
			*a_request.output = g_pipelineFixture->replacement;
		return cs::engine::PixelShaderSwapResolverResult::kReplaced;
	}

	void TestVariantKeySelectsVariant()
	{
		using namespace cs::engine;
		const auto stock = Sha(0x31);
		const std::vector variants{
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(
					shader_variants::kBsdfCompositeAmbientIbl),
				stock,
				0,
				0
			},
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(
					shader_variants::
						kBsdfCompositeAmbientIblTilelight),
				stock,
				0,
				1
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			shader_variants::kBsdfCompositeAmbientIblTilelight,
			stock);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kSelected
				&& selection.routeIndex == 1
				&& selection.replacementIndex == 1,
			"variant key did not select the matching variant");
	}

	void TestMultipleKeysShareReplacement()
	{
		using namespace cs::engine;
		const auto stock = Sha(
			"9969e800683c8a7c8afc25f41582415d79cbe47e");
		const std::vector variants{
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(
					shader_variants::
						kBsdfLightDeferredPoint[0]),
				stock,
				2,
				4
			},
			PixelShaderSwapVariantKey{
				OwnShaderVariantKey(
					shader_variants::
						kBsdfLightDeferredPoint[1]),
				stock,
				2,
				4
			}
		};

		const auto selection = SelectPixelShaderSwapVariant(
			variants,
			shader_variants::kBsdfLightDeferredPoint[1],
			stock);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kSelected
				&& selection.routeIndex == 1
				&& selection.replacementIndex == 4,
			"alias route did not select the shared replacement");
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
				&& selection.routeIndex == 0,
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
				lightHash,
				0,
				1
			}
		};

		const auto lightSelection = SelectPixelShaderSwapVariant(
			variants, light, lightHash);
		Check(
			lightSelection.kind
					== PixelShaderSwapSelectionKind::kSelected
				&& lightSelection.routeIndex == 1
				&& lightSelection.replacementIndex == 1,
			"cross-subclass archive key selected the wrong row");

		const auto mismatch = SelectPixelShaderSwapVariant(
			variants, composite, lightHash);
		Check(
			mismatch.kind
					== PixelShaderSwapSelectionKind::kHashMismatch
				&& mismatch.routeIndex == 0,
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
				&& selection.routeIndex == 0
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
				7,
				1
			}
		};
		const auto selection = SelectPixelShaderSwapVariant(
			variants, pixel, stock);
		Check(
			selection.kind == PixelShaderSwapSelectionKind::kSelected
				&& selection.routeIndex == 1
				&& selection.replacementIndex == 1
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

	void TestBrokerPipelineOrderingAndForwarding()
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
				reinterpret_cast<ID3D11PixelShader*>(&stockToken),
			.replacement =
				reinterpret_cast<ID3D11PixelShader*>(&replacementToken)
		};
		g_pipelineFixture = &fixture;
		const std::array observers{
			PixelShaderSwapObserver{
				.prepare = &PreparePipelineObserver,
				.observeOriginal = &ObservePipelineOriginal,
				.complete = &CompletePipelineObserver
			}
		};
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
		ID3D11PixelShader* output = nullptr;
		cs::sha1::Sha1InitOnce();
		const auto result = ExecutePixelShaderSwapPipeline(
			&PipelineOriginal,
			observers,
			resolvers,
			ShaderVariantKeyView{
				"BSDFLightShader",
				ShaderStage::kPixel,
				ShaderVariantId{ 0x1234 }
			},
			false,
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
			fixture.order == std::vector<int>{ 1, 2, 3, 4, 5 },
			"broker observer/resolver ordering changed");
		Check(
			fixture.resolverForwarded,
			"broker did not forward stock/device/linkage to resolver");
		Check(
			fixture.completion.originalResult == S_OK
				&& fixture.completion.stockOutput == fixture.stock
				&& fixture.completion.finalOutput == fixture.replacement
				&& fixture.completion.resolverInvoked
				&& fixture.completion.resolverReportedReplacement
				&& fixture.completion.finalIsReplacement,
			"broker completion did not preserve stock and final state");

		fixture.order.clear();
		output = nullptr;
		{
			ScopedPixelShaderBrokerBypass bypass;
			Check(
				ExecutePixelShaderSwapPipeline(
					&PipelineOriginal,
					observers,
					resolvers,
					std::nullopt,
					PixelShaderBrokerBypassActive(),
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
			"broker bypass recursed into observers or resolvers");

		fixture.order.clear();
		fixture.originalResult = E_FAIL;
		output = nullptr;
		Check(
			ExecutePixelShaderSwapPipeline(
				&PipelineOriginal,
				observers,
				resolvers,
				std::nullopt,
				false,
				fixture.expectedDevice,
				bytecode.data(),
				bytecode.size(),
				fixture.expectedLinkage,
				&output)
				== E_FAIL,
			"failed stock CreatePS HRESULT was not preserved");
		Check(
			fixture.order == std::vector<int>{ 1, 2, 5 }
				&& !fixture.completion.resolverInvoked
				&& !fixture.completion.finalIsStock
				&& !fixture.completion.finalIsReplacement,
			"failed stock CreatePS invoked resolver or classified stale output");
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
				reinterpret_cast<ID3D11PixelShader*>(&stockToken),
			.replacement =
				reinterpret_cast<ID3D11PixelShader*>(&replacementToken),
			.firstResolverResult =
				PixelShaderSwapResolverResult::kKeepStock
		};
		g_pipelineFixture = &fixture;
		const std::array resolvers{
			PixelShaderSwapResolverRegistration{
				.resolver = &FirstPipelineResolver,
				.priority = kBytecodePatchResolverPriority
			},
			PixelShaderSwapResolverRegistration{
				.resolver = &LowerPipelineResolver,
				.priority = kHlslReplacementResolverPriority
			}
		};
		const std::array<std::byte, 1> bytecode{ std::byte{ 1 } };
		ID3D11PixelShader* output = nullptr;
		Check(
			ExecutePixelShaderSwapPipeline(
				&PipelineOriginal,
				{},
				resolvers,
				std::nullopt,
				false,
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
			PixelShaderSwapResolverResult::kNoMatch;
		fixture.lowerResolverCalled = false;
		output = nullptr;
		Check(
			ExecutePixelShaderSwapPipeline(
				&PipelineOriginal,
				{},
				resolvers,
				std::nullopt,
				false,
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

	void TestDetailedObserverIsSelectionNeutral()
	{
		using namespace cs::engine;
		std::byte deviceToken{};
		std::byte linkageToken{};
		std::byte stockToken{};
		PipelineFixture pipeline{
			.expectedDevice =
				reinterpret_cast<ID3D11Device*>(&deviceToken),
			.expectedLinkage =
				reinterpret_cast<ID3D11ClassLinkage*>(&linkageToken),
			.stock =
				reinterpret_cast<ID3D11PixelShader*>(&stockToken)
		};
		g_pipelineFixture = &pipeline;
		DetailedObserverFixture detailed;
		g_detailedObserverFixture = &detailed;
		const std::array observers{
			PixelShaderSwapObserver{
				.complete = &CompletePipelineObserver,
				.prepareDetailed = &PrepareDetailedObserver
			}
		};
		const std::array<std::byte, 3> bytecode{
			std::byte{ 1 },
			std::byte{ 2 },
			std::byte{ 3 }
		};
		ID3D11PixelShader* output = nullptr;
		const auto result = ExecutePixelShaderSwapPipeline(
			&PipelineOriginal,
			observers,
			{},
			ShaderVariantKeyView{
				"BSDFCompositeShader",
				ShaderStage::kPixel,
				ShaderVariantId{ 0x10B60 }
			},
			false,
			pipeline.expectedDevice,
			bytecode.data(),
			bytecode.size(),
			pipeline.expectedLinkage,
			&output,
			PixelShaderRuntimeRoute{
				.subclass = "BSDFCompositeShader",
				.stage = ShaderStage::kPixel,
				.rawTechnique = 0xB60,
				.pluginResolvedPsid =
					ShaderVariantId{ 0x10B60 },
				.engineLookup =
					EnginePixelShaderLookupObservation{
						.target =
							EnginePixelShaderLookupTarget::
								kBsdfLight,
						.functionInput = 0xB60,
						.returnedPsid =
							EngineLookupPsid{ 0x10B61 },
						.callSequence = 7,
						.threadId = 11
					},
				.tiledLighting = true
			});
		Check(result == S_OK && output == pipeline.stock,
			"detailed observer changed stock result");
		Check(
			detailed.called
				&& detailed.bytecodeLength == bytecode.size()
				&& detailed.classLinkagePresent
				&& detailed.subclass == "BSDFCompositeShader"
				&& detailed.rawTechnique == 0xB60
				&& detailed.pluginResolvedPsid
					== static_cast<std::uint32_t>(0x10B60)
				&& detailed.engineLookupPsid
					== static_cast<std::uint32_t>(0x10B61)
				&& detailed.engineLookupSequence == 7
				&& detailed.tiledLighting == true,
			"detailed observer lost creation provenance");
		Check(
			pipeline.completion.finalIsStock
				&& pipeline.completion.originalInputUnchanged
				&& !pipeline.completion.resolverInvoked,
			"detailed observer changed completion classification");

		pipeline.order.clear();
		pipeline.mutateInput = true;
		output = nullptr;
		auto mutableBytecode = bytecode;
		Check(
			ExecutePixelShaderSwapPipeline(
				&PipelineOriginal,
				observers,
				{},
				std::nullopt,
				false,
				pipeline.expectedDevice,
				mutableBytecode.data(),
				mutableBytecode.size(),
				pipeline.expectedLinkage,
				&output)
				== S_OK
				&& output == pipeline.stock
				&& !pipeline.completion.originalInputUnchanged,
			"broker did not detect changed original input");
	}

	void TestEngineLookupClaimIsIndependentAndOneShot()
	{
		using namespace cs::engine;
		static_assert(
			!std::is_convertible_v<EngineLookupPsid, ShaderVariantId>);
		ResetEnginePixelShaderLookupForTesting();
		std::byte shaderToken{};
		{
			EnginePixelShaderLookupScope scope(
				&shaderToken, "BSDFLightShader", 0x281);
			RecordEnginePixelShaderLookupReturn(
				EnginePixelShaderLookupTarget::kBsdfLight,
				0x281,
				0x201);
			const auto observation =
				ConsumeEnginePixelShaderLookup(
					&shaderToken, "BSDFLightShader", 0x281);
			Check(
				observation
					&& observation->functionInput == 0x281
					&& observation->returnedPsid.Value() == 0x201
					&& observation->callSequence == 1
					&& observation->threadId == GetCurrentThreadId(),
				"engine lookup claim lost its direct return");
			Check(
				!ConsumeEnginePixelShaderLookup(
					&shaderToken, "BSDFLightShader", 0x281),
				"engine lookup claim was consumed twice");
			const auto correlation = scope.Snapshot(
				&shaderToken, "BSDFLightShader", 0x281);
			Check(
				correlation.status
						== EnginePixelShaderLookupCorrelationStatus::kMatched
					&& correlation.reason
						== EnginePixelShaderLookupCorrelationReason::kNone
					&& correlation.observation == observation,
				"lookup snapshot lost a consumed observation");
		}

		const auto testEngineLookupProductionInstallIsBlocked = [] {
			using namespace cs::engine;
			InstallEnginePixelShaderLookupHooks();
			const auto stats =
				GetEnginePixelShaderLookupInstallStats();
			Check(
				GetEnginePixelShaderLookupTargetDescriptors().empty()
					&& stats.attempted == 0
					&& stats.succeeded == 0
					&& stats.failed == 0
					&& !stats.Ready(),
				"production engine lookup hook was enabled without proof");
		};
		testEngineLookupProductionInstallIsBlocked();
		const auto telemetry =
			SnapshotEnginePixelShaderLookupTelemetry();
		Check(
			telemetry.returnsSeen == 1
				&& telemetry.returnsScoped == 1
				&& telemetry.returnsCaptured == 1
				&& telemetry.returnsConsumed == 1
				&& EnginePixelShaderLookupRelationshipsHold(
					telemetry),
			"engine lookup claim telemetry is incoherent");
	}

	void TestEngineLookupClaimFailsClosed()
	{
		using namespace cs::engine;
		ResetEnginePixelShaderLookupForTesting();
		std::byte shaderToken{};
		RecordEnginePixelShaderLookupReturn(
			EnginePixelShaderLookupTarget::kBsdfLight,
			0x281,
			0x201);
		{
			EnginePixelShaderLookupScope scope(
				&shaderToken, "BSDFCompositeShader", 0x281);
			RecordEnginePixelShaderLookupReturn(
				EnginePixelShaderLookupTarget::kBsdfLight,
				0x281,
				0x201);
		}
		{
			EnginePixelShaderLookupScope scope(
				&shaderToken, "BSDFLightShader", 0x281);
			RecordEnginePixelShaderLookupReturn(
				EnginePixelShaderLookupTarget::kBsdfLight,
				0x201,
				0x201);
		}
		{
			EnginePixelShaderLookupScope scope(
				&shaderToken, "BSDFLightShader", 0x281);
			RecordEnginePixelShaderLookupReturn(
				EnginePixelShaderLookupTarget::kBsdfLight,
				0x281,
				0x201);
			RecordEnginePixelShaderLookupReturn(
				EnginePixelShaderLookupTarget::kBsdfLight,
				0x281,
				0x201);
			const auto correlation = scope.Snapshot(
				&shaderToken, "BSDFLightShader", 0x281);
			Check(
				!ConsumeEnginePixelShaderLookup(
					&shaderToken, "BSDFLightShader", 0x281)
					&& correlation.status
						== EnginePixelShaderLookupCorrelationStatus::kAmbiguous
					&& correlation.reason
						== EnginePixelShaderLookupCorrelationReason::
							kMultipleMatchingReturns
					&& !correlation.observation,
				"duplicate engine lookup return remained authoritative");
		}
		const auto telemetry =
			SnapshotEnginePixelShaderLookupTelemetry();
		Check(
			telemetry.discardedOutOfScope == 1
				&& telemetry.discardedSubclassMismatch == 1
				&& telemetry.discardedTechniqueMismatch == 1
				&& telemetry.discardedDuplicate == 1
				&& EnginePixelShaderLookupRelationshipsHold(
					telemetry),
			"engine lookup reject controls were not accounted");
	}

	void TestEngineLookupSnapshotUnavailableAndMismatched()
	{
		using namespace cs::engine;
		ResetEnginePixelShaderLookupForTesting();
		std::byte shaderToken{};
		{
			EnginePixelShaderLookupScope scope(
				&shaderToken, "BSDFLightShader", 0x281);
			const auto missing = scope.Snapshot(
				&shaderToken, "BSDFLightShader", 0x281);
			const auto subclassMismatch = scope.Snapshot(
				&shaderToken, "BSDFCompositeShader", 0x281);
			const auto techniqueMismatch = scope.Snapshot(
				&shaderToken, "BSDFLightShader", 0x282);
			Check(
				missing.status
						== EnginePixelShaderLookupCorrelationStatus::kUnavailable
					&& missing.reason
						== EnginePixelShaderLookupCorrelationReason::
							kProductionLookupHookUnavailable
					&& !missing.observation,
				"unavailable production lookup hook was not explicit");
			Check(
				subclassMismatch.status
						== EnginePixelShaderLookupCorrelationStatus::kRejected
					&& subclassMismatch.reason
						== EnginePixelShaderLookupCorrelationReason::
							kSubclassMismatch
					&& techniqueMismatch.status
						== EnginePixelShaderLookupCorrelationStatus::kRejected
					&& techniqueMismatch.reason
						== EnginePixelShaderLookupCorrelationReason::
							kRawTechniqueMismatch,
				"lookup snapshot accepted a mismatched setup identity");
		}
		{
			EnginePixelShaderLookupScope scope(
				&shaderToken, "BSDFCompositeShader", 0x281);
			const auto unsupported = scope.Snapshot(
				&shaderToken, "BSDFCompositeShader", 0x281);
			Check(
				unsupported.status
						== EnginePixelShaderLookupCorrelationStatus::kUnavailable
					&& unsupported.reason
						== EnginePixelShaderLookupCorrelationReason::
							kNoValidatedTarget,
				"unsupported lookup target was not explicit");
		}
	}

	void TestEngineLookupSnapshotIsNestedAndThreadLocal()
	{
		using namespace cs::engine;
		ResetEnginePixelShaderLookupForTesting();
		std::byte outerShader{};
		std::byte innerShader{};
		EnginePixelShaderLookupScope outer(
			&outerShader, "BSDFLightShader", 0x281);
		RecordEnginePixelShaderLookupReturn(
			EnginePixelShaderLookupTarget::kBsdfLight,
			0x281,
			0x201);
		{
			EnginePixelShaderLookupScope inner(
				&innerShader, "BSDFLightShader", 0x104);
			RecordEnginePixelShaderLookupReturn(
				EnginePixelShaderLookupTarget::kBsdfLight,
				0x104,
				0x104);
			const auto outerDuringInner = outer.Snapshot(
				&outerShader, "BSDFLightShader", 0x281);
			const auto innerCorrelation = inner.Snapshot(
				&innerShader, "BSDFLightShader", 0x104);
			Check(
				outerDuringInner.status
						== EnginePixelShaderLookupCorrelationStatus::kRejected
					&& outerDuringInner.reason
						== EnginePixelShaderLookupCorrelationReason::kOutOfScope
					&& innerCorrelation.status
						== EnginePixelShaderLookupCorrelationStatus::kMatched
					&& innerCorrelation.observation
					&& innerCorrelation.observation->returnedPsid.Value()
						== 0x104,
				"nested lookup snapshot did not enforce exact ownership");
		}
			EnginePixelShaderLookupCorrelationResult foreignThreadCorrelation;
			std::thread other([&] {
				foreignThreadCorrelation = outer.Snapshot(
					&outerShader, "BSDFLightShader", 0x281);
			});
			other.join();
			const auto restored = outer.Snapshot(
				&outerShader, "BSDFLightShader", 0x281);
			Check(
				foreignThreadCorrelation.status
						== EnginePixelShaderLookupCorrelationStatus::kRejected
					&& foreignThreadCorrelation.reason
						== EnginePixelShaderLookupCorrelationReason::kOutOfScope
					&& restored.status
						== EnginePixelShaderLookupCorrelationStatus::kMatched
					&& restored.observation
					&& restored.observation->returnedPsid.Value() == 0x201,
				"lookup snapshot crossed a thread or lost nested restoration");
		}

		struct CurrentPixelShaderFixture
		{
			std::optional<cs::engine::EngineCurrentPixelShaderSnapshot> snapshot;
			unsigned reads = 0;
		};

		std::optional<cs::engine::EngineCurrentPixelShaderSnapshot>
			ReadCurrentPixelShaderFixture(void* a_context) noexcept
		{
			auto& fixture =
				*static_cast<CurrentPixelShaderFixture*>(a_context);
			++fixture.reads;
			return fixture.snapshot;
		}

		void TestCurrentPixelShaderObservationStatuses()
		{
			using namespace cs::engine;
			CurrentPixelShaderFixture nextGen{
				.snapshot = EngineCurrentPixelShaderSnapshot{
					.psid = EngineLookupPsid{ 0x301 },
					.d3dShaderPresent = true
				}
			};
			CurrentPixelShaderFixture anniversary{
				.snapshot = EngineCurrentPixelShaderSnapshot{
					.psid = EngineLookupPsid{ 0x302 },
					.d3dShaderPresent = true
				}
			};
			const auto nextGenResult = ObserveEngineCurrentPixelShader(
				true, &ReadCurrentPixelShaderFixture, &nextGen);
			const auto anniversaryResult = ObserveEngineCurrentPixelShader(
				true, &ReadCurrentPixelShaderFixture, &anniversary);
			Check(
				nextGenResult.status
						== EngineCurrentPixelShaderStatus::kKnown
					&& nextGenResult.psid
					&& nextGenResult.psid->Value() == 0x301
					&& nextGen.reads == 1
					&& anniversaryResult.status
						== EngineCurrentPixelShaderStatus::kKnown
					&& anniversaryResult.psid
					&& anniversaryResult.psid->Value() == 0x302
					&& anniversary.reads == 1,
				"NG or AE current wrapper did not produce a known PSID");

			const auto originalReads = anniversary.reads;
			const auto originalGameResult = ObserveEngineCurrentPixelShader(
				false, &ReadCurrentPixelShaderFixture, &anniversary);
			Check(
				originalGameResult.status
						== EngineCurrentPixelShaderStatus::
							kUnavailableOnRuntime
					&& !originalGameResult.psid
					&& anniversary.reads == originalReads,
				"OG current wrapper attempted to use its unresolved relocation");

			CurrentPixelShaderFixture noWrapper;
			const auto noWrapperResult = ObserveEngineCurrentPixelShader(
				true, &ReadCurrentPixelShaderFixture, &noWrapper);
			Check(
				noWrapperResult.status
						== EngineCurrentPixelShaderStatus::kNoCurrentPixelShader
					&& !noWrapperResult.psid
					&& noWrapper.reads == 1,
				"null current wrapper was not explicit");

			CurrentPixelShaderFixture noD3DShader{
				.snapshot = EngineCurrentPixelShaderSnapshot{
					.psid = EngineLookupPsid{ 0x303 },
					.d3dShaderPresent = false
				}
			};
			const auto noD3DResult = ObserveEngineCurrentPixelShader(
				true, &ReadCurrentPixelShaderFixture, &noD3DShader);
			Check(
				noD3DResult.status
						== EngineCurrentPixelShaderStatus::kNoD3DShader
					&& noD3DResult.psid
					&& noD3DResult.psid->Value() == 0x303,
				"null D3D shader lost the independently valid wrapper PSID");
		}

		void TestEnginePixelShaderIdentityReconciliation()
		{
			using namespace cs::engine;
			const EnginePixelShaderLookupObservation lookupObservation{
				.target = EnginePixelShaderLookupTarget::kBsdfLight,
				.functionInput = 0x281,
				.returnedPsid = EngineLookupPsid{ 0x201 },
				.callSequence = 1,
				.threadId = 2
			};
			const EnginePixelShaderLookupCorrelationResult matched{
				.status = EnginePixelShaderLookupCorrelationStatus::kMatched,
				.reason = EnginePixelShaderLookupCorrelationReason::kNone,
				.observation = lookupObservation
			};
			const EngineCurrentPixelShaderObservation sameWrapper{
				.status = EngineCurrentPixelShaderStatus::kKnown,
				.psid = EngineLookupPsid{ 0x201 }
			};
			const auto same = ReconcileEnginePixelShaderId(
				matched, sameWrapper);
			Check(
				same.status == EnginePixelShaderIdentityStatus::kMatched
					&& same.source
						== EnginePixelShaderIdentitySource::
							kLookupReturnAndCurrentWrapper
					&& same.reason == EnginePixelShaderIdentityReason::kNone
					&& same.psid
					&& same.psid->Value() == 0x201,
				"matching lookup and wrapper evidence did not reconcile");

			auto differentWrapper = sameWrapper;
			differentWrapper.psid = EngineLookupPsid{ 0x202 };
			const auto mismatched = ReconcileEnginePixelShaderId(
				matched, differentWrapper);
			Check(
				mismatched.status
						== EnginePixelShaderIdentityStatus::kRejected
					&& mismatched.source
						== EnginePixelShaderIdentitySource::
							kLookupReturnAndCurrentWrapper
					&& mismatched.reason
						== EnginePixelShaderIdentityReason::
							kLookupCurrentWrapperMismatch
					&& !mismatched.psid,
				"lookup and wrapper disagreement selected a PSID");

			const EnginePixelShaderLookupCorrelationResult ambiguous{
				.status = EnginePixelShaderLookupCorrelationStatus::kAmbiguous,
				.reason = EnginePixelShaderLookupCorrelationReason::
					kMultipleMatchingReturns
			};
			const auto unresolved = ReconcileEnginePixelShaderId(
				ambiguous, sameWrapper);
			Check(
				unresolved.status
						== EnginePixelShaderIdentityStatus::kAmbiguous
					&& unresolved.source
						== EnginePixelShaderIdentitySource::
							kLookupReturnAndCurrentWrapper
					&& unresolved.reason
						== EnginePixelShaderIdentityReason::
							kMultipleMatchingReturns
					&& !unresolved.psid,
				"known wrapper weakened ambiguous lookup behavior");

			const EnginePixelShaderLookupCorrelationResult unavailable{
				.status = EnginePixelShaderLookupCorrelationStatus::kUnavailable,
				.reason = EnginePixelShaderLookupCorrelationReason::
					kProductionLookupHookUnavailable
			};
			const auto wrapperOnly = ReconcileEnginePixelShaderId(
				unavailable, sameWrapper);
			Check(
				wrapperOnly.status == EnginePixelShaderIdentityStatus::kKnown
					&& wrapperOnly.source
						== EnginePixelShaderIdentitySource::kCurrentWrapper
					&& wrapperOnly.reason
						== EnginePixelShaderIdentityReason::kNone
					&& wrapperOnly.psid
					&& wrapperOnly.psid->Value() == 0x201,
				"current wrapper did not replace an unavailable lookup source");

			const EngineCurrentPixelShaderObservation noD3DShader{
				.status = EngineCurrentPixelShaderStatus::kNoD3DShader,
				.psid = EngineLookupPsid{ 0x203 }
			};
			const auto wrapperWithoutD3D = ReconcileEnginePixelShaderId(
				unavailable, noD3DShader);
			Check(
				wrapperWithoutD3D.status
						== EnginePixelShaderIdentityStatus::kKnown
					&& wrapperWithoutD3D.source
						== EnginePixelShaderIdentitySource::kCurrentWrapper
					&& wrapperWithoutD3D.reason
						== EnginePixelShaderIdentityReason::kNoD3DShader
					&& wrapperWithoutD3D.psid
					&& wrapperWithoutD3D.psid->Value() == 0x203,
				"wrapper PSID incorrectly depended on its D3D shader");

			const EngineCurrentPixelShaderObservation originalGame{
				.status =
					EngineCurrentPixelShaderStatus::kUnavailableOnRuntime
			};
			const auto originalGameResult = ReconcileEnginePixelShaderId(
				unavailable, originalGame);
			const EngineCurrentPixelShaderObservation noWrapper{
				.status =
					EngineCurrentPixelShaderStatus::kNoCurrentPixelShader
			};
			const auto noWrapperResult = ReconcileEnginePixelShaderId(
				unavailable, noWrapper);
			Check(
				originalGameResult.status
						== EnginePixelShaderIdentityStatus::kUnavailable
					&& originalGameResult.source
						== EnginePixelShaderIdentitySource::kNone
					&& originalGameResult.reason
						== EnginePixelShaderIdentityReason::
							kUnavailableOnRuntime
					&& !originalGameResult.psid
					&& noWrapperResult.status
						== EnginePixelShaderIdentityStatus::kUnavailable
					&& noWrapperResult.reason
						== EnginePixelShaderIdentityReason::
							kNoCurrentPixelShader
					&& !noWrapperResult.psid,
				"unavailable current wrapper state was not explicit");
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
		{ "variant key selects variant", &TestVariantKeySelectsVariant },
		{ "multiple keys share replacement", &TestMultipleKeysShareReplacement },
		{ "variant hash mismatch refused", &TestVariantHashMismatchRefused },
		{ "cross-subclass key collision guarded", &TestCrossSubclassKeyCollisionStaysGuarded },
		{ "hashless variant refused", &TestHashlessVariantRefused },
		{ "unmapped variant remains stock", &TestUnmappedVariantRemainsStock },
		{ "unavailable resolution falls back", &TestUnavailableResolutionFallsBackToHash },
		{ "variant key scope includes stage", &TestVariantKeyScopeIncludesStage },
		{ "composite unresolved state unavailable", &TestCompositeResolutionStaysUnavailable },
		{ "composite resolver masks technique", &TestCompositeResolverMasksAndForcesTilelight },
		{ "Light resolver masks technique", &TestBsdfLightResolverMasksTechnique },
		{ "not-ready replacement keeps stock", &TestNotReadyReplacementKeepsStock },
		{ "broker pipeline ordering and forwarding", &TestBrokerPipelineOrderingAndForwarding },
		{ "resolver claim stops lower priority", &TestResolverClaimStopsLowerPriority },
		{ "detailed observer is selection neutral", &TestDetailedObserverIsSelectionNeutral },
		{ "engine lookup claim is independent", &TestEngineLookupClaimIsIndependentAndOneShot },
		{ "engine lookup claim fails closed", &TestEngineLookupClaimFailsClosed },
		{ "engine lookup snapshot unavailable", &TestEngineLookupSnapshotUnavailableAndMismatched },
		{ "engine lookup snapshot is thread local", &TestEngineLookupSnapshotIsNestedAndThreadLocal },
		{ "current wrapper statuses", &TestCurrentPixelShaderObservationStatuses },
		{ "engine PSID reconciliation", &TestEnginePixelShaderIdentityReconciliation },
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
