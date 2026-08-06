#include "Render/ShaderMacroDiagnosticsModel.h"

#include <nlohmann/json.hpp>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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

	void CheckRuntimeHalfJson(const std::string& a_line)
	{
		constexpr std::string_view prefix = "CS_SHADER_RUNTIME_HALF ";
		Check(
			a_line.rfind(prefix, 0) == 0,
			"runtime-half line prefix changed");
		const auto document = nlohmann::json::parse(
			a_line.substr(prefix.size()));
		Check(
			document.is_object()
				&& document.at("schema")
					== "fo4cs.shader-route-runtime-half"
				&& document.at("schema_version") == 1,
			"runtime-half payload is not valid schema-v1 JSON");
	}

	void TestExactLightLines()
	{
		using namespace cs::engine;
		const std::array macros{
			ShaderMacroDefinitionView{ "LIGHT_TYPE", "3" },
			ShaderMacroDefinitionView{ "SHADOW", "" },
			ShaderMacroDefinitionView{ "FILTER", "PCF9" }
		};
		const LightSetupTupleKeyView known{
			.subclass = "BSDFLightShader",
			.rawTechnique = 641,
			.engineLookupPsid = 513,
			.pluginResolvedPsid = 514,
			.engineLookupSource =
				EnginePixelShaderIdentitySource::
					kLookupReturnAndCurrentWrapper,
			.correlationStatus =
				EnginePixelShaderIdentityStatus::kMatched,
			.correlationReason =
				EnginePixelShaderIdentityReason::kNone,
			.macros = macros
		};
		const auto knownLine = FormatLightRuntimeHalfLine(known);
		Check(
			knownLine
				== "CS_SHADER_RUNTIME_HALF {\"schema\":"
				   "\"fo4cs.shader-route-runtime-half\","
				   "\"schema_version\":1,"
				   "\"vantage\":\"setup_technique\","
				   "\"family\":\"light\","
				   "\"subclass\":\"BSDFLightShader\","
				   "\"raw_technique\":641,"
				   "\"engine_lookup_psid\":513,"
				   "\"engine_lookup_source\":"
				   "\"lookup_return_and_current_wrapper\","
				   "\"plugin_resolved_psid\":514,"
				   "\"correlation_status\":\"matched\","
				   "\"correlation_reason\":null,"
				   "\"macros\":[{\"name\":\"LIGHT_TYPE\","
				   "\"value\":\"3\"},{\"name\":\"SHADOW\","
				   "\"value\":\"\"},{\"name\":\"FILTER\","
				   "\"value\":\"PCF9\"}],"
				   "\"tiled_lighting\":null,"
				   "\"rgbspec_global_byte\":null}",
			"known Light runtime-half line changed");
		CheckRuntimeHalfJson(knownLine);

		auto unknown = known;
		unknown.engineLookupPsid = std::nullopt;
		unknown.pluginResolvedPsid = std::nullopt;
		unknown.engineLookupSource =
			EnginePixelShaderIdentitySource::kNone;
		unknown.correlationStatus =
			EnginePixelShaderIdentityStatus::kUnavailable;
		unknown.correlationReason =
			EnginePixelShaderIdentityReason::
				kProductionLookupHookUnavailable;
		const auto unknownLine = FormatLightRuntimeHalfLine(unknown);
		Check(
			unknownLine
				== "CS_SHADER_RUNTIME_HALF {\"schema\":"
				   "\"fo4cs.shader-route-runtime-half\","
				   "\"schema_version\":1,"
				   "\"vantage\":\"setup_technique\","
				   "\"family\":\"light\","
				   "\"subclass\":\"BSDFLightShader\","
				   "\"raw_technique\":641,"
				   "\"engine_lookup_psid\":null,"
				   "\"engine_lookup_source\":\"none\","
				   "\"plugin_resolved_psid\":null,"
				   "\"correlation_status\":\"unavailable\","
				   "\"correlation_reason\":"
				   "\"production_lookup_hook_unavailable\","
				   "\"macros\":[{\"name\":\"LIGHT_TYPE\","
				   "\"value\":\"3\"},{\"name\":\"SHADOW\","
				   "\"value\":\"\"},{\"name\":\"FILTER\","
				   "\"value\":\"PCF9\"}],"
				   "\"tiled_lighting\":null,"
				   "\"rgbspec_global_byte\":null}",
			"unknown PSIDs were not formatted as JSON null");
		CheckRuntimeHalfJson(unknownLine);
	}

	void TestExactCompositeLines()
	{
		using namespace cs::engine;
		const CompositeSetupTupleKeyView unavailable{
			.subclass = "BSDFCompositeShader",
			.rawTechnique = 123,
			.engineLookupPsid = std::nullopt,
			.pluginResolvedPsid = 45,
			.engineLookupSource =
				EnginePixelShaderIdentitySource::kNone,
			.correlationStatus =
				EnginePixelShaderIdentityStatus::kUnavailable,
			.correlationReason =
				EnginePixelShaderIdentityReason::kNoValidatedTarget,
			.tiledLighting = true,
			.rgbspecGlobalByte = std::nullopt
		};
		const auto unavailableLine =
			FormatCompositeRuntimeHalfLine(unavailable);
		Check(
			unavailableLine
				== "CS_SHADER_RUNTIME_HALF {\"schema\":"
				   "\"fo4cs.shader-route-runtime-half\","
				   "\"schema_version\":1,"
				   "\"vantage\":\"setup_technique\","
				   "\"family\":\"composite\","
				   "\"subclass\":\"BSDFCompositeShader\","
				   "\"raw_technique\":123,"
				   "\"engine_lookup_psid\":null,"
				   "\"engine_lookup_source\":\"none\","
				   "\"plugin_resolved_psid\":45,"
				   "\"correlation_status\":\"unavailable\","
				   "\"correlation_reason\":\"no_validated_target\","
				   "\"macros\":null,"
				   "\"tiled_lighting\":true,"
				   "\"rgbspec_global_byte\":null}",
			"Composite runtime-half line changed");
		CheckRuntimeHalfJson(unavailableLine);

		auto future = unavailable;
		future.tiledLighting = false;
		future.rgbspecGlobalByte = static_cast<std::uint8_t>(173);
		const auto futureLine = FormatCompositeRuntimeHalfLine(future);
		Check(
			futureLine
				== "CS_SHADER_RUNTIME_HALF {\"schema\":"
				   "\"fo4cs.shader-route-runtime-half\","
				   "\"schema_version\":1,"
				   "\"vantage\":\"setup_technique\","
				   "\"family\":\"composite\","
				   "\"subclass\":\"BSDFCompositeShader\","
				   "\"raw_technique\":123,"
				   "\"engine_lookup_psid\":null,"
				   "\"engine_lookup_source\":\"none\","
				   "\"plugin_resolved_psid\":45,"
				   "\"correlation_status\":\"unavailable\","
				   "\"correlation_reason\":\"no_validated_target\","
				   "\"macros\":null,"
				   "\"tiled_lighting\":false,"
				   "\"rgbspec_global_byte\":173}",
			"Composite tile or RGBSPEC formatting changed");
		CheckRuntimeHalfJson(futureLine);
	}

	void TestLightTupleStore()
	{
		using namespace cs::engine;
		const std::array macros{
			ShaderMacroDefinitionView{ "LIGHT_TYPE", "2" },
			ShaderMacroDefinitionView{ "SHADOW", "1" }
		};
		const std::array changedMacros{
			ShaderMacroDefinitionView{ "LIGHT_TYPE", "3" },
			ShaderMacroDefinitionView{ "SHADOW", "1" }
		};
		LightSetupTupleKeyView key{
			.subclass = "BSDFLightShader",
			.rawTechnique = 7,
			.engineLookupPsid = 11,
			.pluginResolvedPsid = 12,
			.engineLookupSource =
				EnginePixelShaderIdentitySource::kLookupReturn,
			.correlationStatus =
				EnginePixelShaderIdentityStatus::kMatched,
			.correlationReason =
				EnginePixelShaderIdentityReason::kNone,
			.macros = macros
		};
		LightSetupTupleStore store(5);
		const auto first = store.Insert(key);
		const auto duplicate = store.Insert(key);
		key.pluginResolvedPsid = 13;
		const auto changedPsid = store.Insert(key);
		key.macros = changedMacros;
		const auto changedMacroTable = store.Insert(key);
		key.engineLookupSource =
			EnginePixelShaderIdentitySource::
				kLookupReturnAndCurrentWrapper;
		const auto changedSource = store.Insert(key);
		key.correlationStatus =
			EnginePixelShaderIdentityStatus::kAmbiguous;
		key.correlationReason =
			EnginePixelShaderIdentityReason::kMultipleMatchingReturns;
		const auto changedCorrelation = store.Insert(key);
		Check(
			first.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& duplicate.result
					== ShaderMacroDiagnosticsInsertResult::kDuplicate
				&& changedPsid.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& changedMacroTable.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& changedSource.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& changedCorrelation.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& store.Size() == 5
				&& store.MacroSetCount() == 2,
			"Light setup tuple key omitted a route field");
	}

	void TestCompositeTupleStore()
	{
		using namespace cs::engine;
		CompositeSetupTupleKeyView key{
			.subclass = "BSDFCompositeShader",
			.rawTechnique = 19,
			.engineLookupPsid = std::nullopt,
			.pluginResolvedPsid = 21,
			.engineLookupSource =
				EnginePixelShaderIdentitySource::kNone,
			.correlationStatus =
				EnginePixelShaderIdentityStatus::kUnavailable,
			.correlationReason =
				EnginePixelShaderIdentityReason::kNoValidatedTarget,
			.tiledLighting = true,
			.rgbspecGlobalByte = std::nullopt
		};
		CompositeSetupTupleStore store(4);
		const auto first = store.Insert(key);
		const auto duplicate = store.Insert(key);
		key.tiledLighting = false;
		const auto changedTile = store.Insert(key);
		key.rgbspecGlobalByte = static_cast<std::uint8_t>(1);
		const auto changedRgbspec = store.Insert(key);
		key.pluginResolvedPsid = 22;
		const auto changedPsid = store.Insert(key);
		Check(
			first.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& duplicate.result
					== ShaderMacroDiagnosticsInsertResult::kDuplicate
				&& changedTile.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& changedRgbspec.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& changedPsid.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight
				&& store.Size() == 4,
			"Composite setup tuple key omitted a route field");
	}

	void TestCapacityGuards()
	{
		using namespace cs::engine;
		static_assert(kLightSetupTupleCapacity == 512);
		static_assert(kCompositeSetupTupleCapacity == 512);
		const std::array macros{
			ShaderMacroDefinitionView{ "LIGHT_TYPE", "1" }
		};
		LightSetupTupleKeyView light{
			.subclass = "BSDFLightShader",
			.macros = macros
		};
		LightSetupTupleStore lights;
		for (std::size_t index = 0;
			index < kLightSetupTupleCapacity;
			++index) {
			light.rawTechnique = static_cast<std::uint32_t>(index);
			Check(
				lights.Insert(light).result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight,
				"Light capacity fixture rejected an in-range tuple");
		}
		light.rawTechnique =
			static_cast<std::uint32_t>(kLightSetupTupleCapacity);
		Check(
			lights.Insert(light).result
					== ShaderMacroDiagnosticsInsertResult::kCapacityExceeded
				&& lights.Size() == kLightSetupTupleCapacity,
			"Light capacity guard did not fail closed");
		light.rawTechnique = 0;
		Check(
			lights.Insert(light).result
				== ShaderMacroDiagnosticsInsertResult::kDuplicate,
			"Light full store stopped recognizing duplicates");

		CompositeSetupTupleKeyView composite{
			.subclass = "BSDFCompositeShader"
		};
		CompositeSetupTupleStore composites;
		for (std::size_t index = 0;
			index < kCompositeSetupTupleCapacity;
			++index) {
			composite.rawTechnique = static_cast<std::uint32_t>(index);
			Check(
				composites.Insert(composite).result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight,
				"Composite capacity fixture rejected an in-range tuple");
		}
		composite.rawTechnique =
			static_cast<std::uint32_t>(kCompositeSetupTupleCapacity);
		Check(
			composites.Insert(composite).result
					== ShaderMacroDiagnosticsInsertResult::kCapacityExceeded
				&& composites.Size() == kCompositeSetupTupleCapacity,
			"Composite capacity guard did not fail closed");
		composite.rawTechnique = 0;
		Check(
			composites.Insert(composite).result
				== ShaderMacroDiagnosticsInsertResult::kDuplicate,
			"Composite full store stopped recognizing duplicates");
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
		{ "exact Light lines", &TestExactLightLines },
		{ "exact Composite lines", &TestExactCompositeLines },
		{ "Light tuple store", &TestLightTupleStore },
		{ "Composite tuple store", &TestCompositeTupleStore },
		{ "capacity guards", &TestCapacityGuards }
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
