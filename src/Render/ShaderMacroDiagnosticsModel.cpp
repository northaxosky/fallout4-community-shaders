#include "Render/ShaderMacroDiagnosticsModel.h"

#include <charconv>

namespace cs::engine
{
	namespace
	{
		bool MacroTablesMatch(
			std::span<const ShaderMacroDefinition> a_left,
			std::span<const ShaderMacroDefinitionView> a_right) noexcept
		{
			if (a_left.size() != a_right.size())
				return false;
			for (std::size_t index = 0; index < a_left.size(); ++index) {
				if (a_left[index].name != a_right[index].name
					|| a_left[index].value != a_right[index].value) {
					return false;
				}
			}
			return true;
		}

		LightSetupTupleKey Own(const LightSetupTupleKeyView& a_key)
		{
			LightSetupTupleKey result{
				.subclass = std::string(a_key.subclass),
				.rawTechnique = a_key.rawTechnique,
				.engineLookupPsid = a_key.engineLookupPsid,
				.pluginResolvedPsid = a_key.pluginResolvedPsid,
				.correlationStatus = a_key.correlationStatus,
				.correlationReason = a_key.correlationReason
			};
			result.macros.reserve(a_key.macros.size());
			for (const auto& macro : a_key.macros) {
				result.macros.push_back({
					.name = std::string(macro.name),
					.value = std::string(macro.value)
				});
			}
			return result;
		}

		CompositeSetupTupleKey Own(
			const CompositeSetupTupleKeyView& a_key)
		{
			return {
				.subclass = std::string(a_key.subclass),
				.rawTechnique = a_key.rawTechnique,
				.engineLookupPsid = a_key.engineLookupPsid,
				.pluginResolvedPsid = a_key.pluginResolvedPsid,
				.correlationStatus = a_key.correlationStatus,
				.correlationReason = a_key.correlationReason,
				.tiledLighting = a_key.tiledLighting,
				.rgbspecGlobalByte = a_key.rgbspecGlobalByte
			};
		}

		void AppendJsonString(
			std::string& a_output,
			std::string_view a_value)
		{
			constexpr char hex[] = "0123456789abcdef";
			a_output.push_back('"');
			for (const char rawValue : a_value) {
				const auto value =
					static_cast<unsigned char>(rawValue);
				switch (value) {
				case '"':
					a_output += "\\\"";
					break;
				case '\\':
					a_output += "\\\\";
					break;
				case '\b':
					a_output += "\\b";
					break;
				case '\f':
					a_output += "\\f";
					break;
				case '\n':
					a_output += "\\n";
					break;
				case '\r':
					a_output += "\\r";
					break;
				case '\t':
					a_output += "\\t";
					break;
				default:
					if (value < 0x20) {
						a_output += "\\u00";
						a_output.push_back(hex[value >> 4]);
						a_output.push_back(hex[value & 0x0f]);
					} else {
						a_output.push_back(static_cast<char>(value));
					}
					break;
				}
			}
			a_output.push_back('"');
		}

		template <class Integer>
		void AppendInteger(std::string& a_output, Integer a_value)
		{
			char buffer[32]{};
			const auto [end, error] = std::to_chars(
				buffer, buffer + sizeof(buffer), a_value);
			if (error == std::errc{})
				a_output.append(buffer, end);
		}

		template <class Integer>
		void AppendOptionalInteger(
			std::string& a_output,
			std::optional<Integer> a_value)
		{
			if (a_value)
				AppendInteger(a_output, *a_value);
			else
				a_output += "null";
		}

		std::string StartRuntimeHalfLine(
			std::string_view a_family,
			std::string_view a_subclass,
			std::uint32_t a_rawTechnique,
			std::optional<std::uint32_t> a_engineLookupPsid,
			std::optional<std::uint32_t> a_pluginResolvedPsid,
			EnginePixelShaderLookupCorrelationStatus a_correlationStatus,
			EnginePixelShaderLookupCorrelationReason a_correlationReason)
		{
			std::string result;
			result.reserve(512);
			result =
				"CS_SHADER_RUNTIME_HALF {\"schema\":"
				"\"fo4cs.shader-route-runtime-half\","
				"\"schema_version\":1,"
				"\"vantage\":\"setup_technique\",\"family\":";
			AppendJsonString(result, a_family);
			result += ",\"subclass\":";
			AppendJsonString(result, a_subclass);
			result += ",\"raw_technique\":";
			AppendInteger(result, a_rawTechnique);
			result += ",\"engine_lookup_psid\":";
			AppendOptionalInteger(result, a_engineLookupPsid);
			result += ",\"plugin_resolved_psid\":";
			AppendOptionalInteger(result, a_pluginResolvedPsid);
			result += ",\"correlation_status\":";
			AppendJsonString(
				result,
				EnginePixelShaderLookupCorrelationStatusName(
					a_correlationStatus));
			result += ",\"correlation_reason\":";
			if (a_correlationReason
				== EnginePixelShaderLookupCorrelationReason::kNone) {
				result += "null";
			} else {
				AppendJsonString(
					result,
					EnginePixelShaderLookupCorrelationReasonName(
						a_correlationReason));
			}
			return result;
		}
	}

	bool LightSetupTupleKey::Matches(
		const LightSetupTupleKeyView& a_other) const noexcept
	{
		return subclass == a_other.subclass
			&& rawTechnique == a_other.rawTechnique
			&& engineLookupPsid == a_other.engineLookupPsid
			&& pluginResolvedPsid == a_other.pluginResolvedPsid
			&& correlationStatus == a_other.correlationStatus
			&& correlationReason == a_other.correlationReason
			&& MacroTablesMatch(macros, a_other.macros);
	}

	bool CompositeSetupTupleKey::Matches(
		const CompositeSetupTupleKeyView& a_other) const noexcept
	{
		return subclass == a_other.subclass
			&& rawTechnique == a_other.rawTechnique
			&& engineLookupPsid == a_other.engineLookupPsid
			&& pluginResolvedPsid == a_other.pluginResolvedPsid
			&& correlationStatus == a_other.correlationStatus
			&& correlationReason == a_other.correlationReason
			&& tiledLighting == a_other.tiledLighting
			&& rgbspecGlobalByte == a_other.rgbspecGlobalByte;
	}

	LightSetupTupleStore::LightSetupTupleStore(
		std::size_t a_capacity) noexcept :
		_capacity(a_capacity)
	{}

	ShaderMacroDiagnosticsInsert LightSetupTupleStore::Insert(
		const LightSetupTupleKeyView& a_key)
	{
		for (std::size_t index = 0; index < _records.size(); ++index) {
			const auto& record = _records[index];
			if (record.key.Matches(a_key)) {
				return {
					.result =
						ShaderMacroDiagnosticsInsertResult::kDuplicate,
					.tupleIndex = index + 1,
					.macroSetIndex = record.macroSetIndex
				};
			}
		}
		if (_records.size() == _capacity) {
			return {
				.result = ShaderMacroDiagnosticsInsertResult::
					kCapacityExceeded
			};
		}

		std::size_t macroSetIndex = 0;
		for (const auto& record : _records) {
			if (MacroTablesMatch(record.key.macros, a_key.macros)) {
				macroSetIndex = record.macroSetIndex;
				break;
			}
		}
		if (macroSetIndex == 0)
			macroSetIndex = ++_macroSetCount;

		_records.push_back({
			.key = Own(a_key),
			.macroSetIndex = macroSetIndex
		});
		return {
			.result = ShaderMacroDiagnosticsInsertResult::kFirstSight,
			.tupleIndex = _records.size(),
			.macroSetIndex = macroSetIndex
		};
	}

	std::size_t LightSetupTupleStore::Size() const noexcept
	{
		return _records.size();
	}

	std::size_t LightSetupTupleStore::MacroSetCount() const noexcept
	{
		return _macroSetCount;
	}

	CompositeSetupTupleStore::CompositeSetupTupleStore(
		std::size_t a_capacity) noexcept :
		_capacity(a_capacity)
	{}

	ShaderMacroDiagnosticsInsert CompositeSetupTupleStore::Insert(
		const CompositeSetupTupleKeyView& a_key)
	{
		for (std::size_t index = 0; index < _records.size(); ++index) {
			if (_records[index].Matches(a_key)) {
				return {
					.result =
						ShaderMacroDiagnosticsInsertResult::kDuplicate,
					.tupleIndex = index + 1
				};
			}
		}
		if (_records.size() == _capacity) {
			return {
				.result = ShaderMacroDiagnosticsInsertResult::
					kCapacityExceeded
			};
		}

		_records.push_back(Own(a_key));
		return {
			.result = ShaderMacroDiagnosticsInsertResult::kFirstSight,
			.tupleIndex = _records.size()
		};
	}

	std::size_t CompositeSetupTupleStore::Size() const noexcept
	{
		return _records.size();
	}

	std::string FormatLightRuntimeHalfLine(
		const LightSetupTupleKeyView& a_key)
	{
		auto result = StartRuntimeHalfLine(
			"light",
			a_key.subclass,
			a_key.rawTechnique,
			a_key.engineLookupPsid,
			a_key.pluginResolvedPsid,
			a_key.correlationStatus,
			a_key.correlationReason);
		result += ",\"macros\":[";
		for (std::size_t index = 0; index < a_key.macros.size(); ++index) {
			if (index != 0)
				result.push_back(',');
			result += "{\"name\":";
			AppendJsonString(result, a_key.macros[index].name);
			result += ",\"value\":";
			AppendJsonString(result, a_key.macros[index].value);
			result.push_back('}');
		}
		result +=
			"],\"tiled_lighting\":null,"
			"\"rgbspec_global_byte\":null}";
		return result;
	}

	std::string FormatCompositeRuntimeHalfLine(
		const CompositeSetupTupleKeyView& a_key)
	{
		auto result = StartRuntimeHalfLine(
			"composite",
			a_key.subclass,
			a_key.rawTechnique,
			a_key.engineLookupPsid,
			a_key.pluginResolvedPsid,
			a_key.correlationStatus,
			a_key.correlationReason);
		result += ",\"macros\":null,\"tiled_lighting\":";
		if (a_key.tiledLighting)
			result += *a_key.tiledLighting ? "true" : "false";
		else
			result += "null";
		result += ",\"rgbspec_global_byte\":";
		AppendOptionalInteger(result, a_key.rgbspecGlobalByte);
		result.push_back('}');
		return result;
	}
}
