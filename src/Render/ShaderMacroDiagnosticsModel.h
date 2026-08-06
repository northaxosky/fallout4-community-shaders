#pragma once

#include "Render/EnginePixelShaderLookup.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cs::engine
{
	inline constexpr std::size_t kLightSetupTupleCapacity = 512;
	inline constexpr std::size_t kCompositeSetupTupleCapacity = 512;

	struct ShaderMacroDefinitionView
	{
		std::string_view name;
		std::string_view value;
	};

	struct ShaderMacroDefinition
	{
		std::string name;
		std::string value;
	};

	struct LightSetupTupleKeyView
	{
		std::string_view subclass;
		std::uint32_t rawTechnique = 0;
		std::optional<std::uint32_t> engineLookupPsid;
		std::optional<std::uint32_t> pluginResolvedPsid;
		EnginePixelShaderLookupCorrelationStatus correlationStatus =
			EnginePixelShaderLookupCorrelationStatus::kUnavailable;
		EnginePixelShaderLookupCorrelationReason correlationReason =
			EnginePixelShaderLookupCorrelationReason::kNoLookupObservation;
		std::span<const ShaderMacroDefinitionView> macros;
	};

	struct LightSetupTupleKey
	{
		std::string subclass;
		std::uint32_t rawTechnique = 0;
		std::optional<std::uint32_t> engineLookupPsid;
		std::optional<std::uint32_t> pluginResolvedPsid;
		EnginePixelShaderLookupCorrelationStatus correlationStatus =
			EnginePixelShaderLookupCorrelationStatus::kUnavailable;
		EnginePixelShaderLookupCorrelationReason correlationReason =
			EnginePixelShaderLookupCorrelationReason::kNoLookupObservation;
		std::vector<ShaderMacroDefinition> macros;

		[[nodiscard]] bool Matches(
			const LightSetupTupleKeyView& a_other) const noexcept;
	};

	struct CompositeSetupTupleKeyView
	{
		std::string_view subclass;
		std::uint32_t rawTechnique = 0;
		std::optional<std::uint32_t> engineLookupPsid;
		std::optional<std::uint32_t> pluginResolvedPsid;
		EnginePixelShaderLookupCorrelationStatus correlationStatus =
			EnginePixelShaderLookupCorrelationStatus::kUnavailable;
		EnginePixelShaderLookupCorrelationReason correlationReason =
			EnginePixelShaderLookupCorrelationReason::kNoValidatedTarget;
		std::optional<bool> tiledLighting;
		std::optional<std::uint8_t> rgbspecGlobalByte;
	};

	struct CompositeSetupTupleKey
	{
		std::string subclass;
		std::uint32_t rawTechnique = 0;
		std::optional<std::uint32_t> engineLookupPsid;
		std::optional<std::uint32_t> pluginResolvedPsid;
		EnginePixelShaderLookupCorrelationStatus correlationStatus =
			EnginePixelShaderLookupCorrelationStatus::kUnavailable;
		EnginePixelShaderLookupCorrelationReason correlationReason =
			EnginePixelShaderLookupCorrelationReason::kNoValidatedTarget;
		std::optional<bool> tiledLighting;
		std::optional<std::uint8_t> rgbspecGlobalByte;

		[[nodiscard]] bool Matches(
			const CompositeSetupTupleKeyView& a_other) const noexcept;
	};

	enum class ShaderMacroDiagnosticsInsertResult : std::uint8_t
	{
		kFirstSight,
		kDuplicate,
		kCapacityExceeded
	};

	struct ShaderMacroDiagnosticsInsert
	{
		ShaderMacroDiagnosticsInsertResult result =
			ShaderMacroDiagnosticsInsertResult::kCapacityExceeded;
		std::size_t tupleIndex = 0;
		std::size_t macroSetIndex = 0;
	};

	class LightSetupTupleStore
	{
	public:
		explicit LightSetupTupleStore(
			std::size_t a_capacity = kLightSetupTupleCapacity) noexcept;

		[[nodiscard]] ShaderMacroDiagnosticsInsert Insert(
			const LightSetupTupleKeyView& a_key);
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] std::size_t MacroSetCount() const noexcept;

	private:
		struct Record
		{
			LightSetupTupleKey key;
			std::size_t macroSetIndex = 0;
		};

		std::size_t _capacity = 0;
		std::size_t _macroSetCount = 0;
		std::vector<Record> _records;
	};

	class CompositeSetupTupleStore
	{
	public:
		explicit CompositeSetupTupleStore(
			std::size_t a_capacity = kCompositeSetupTupleCapacity) noexcept;

		[[nodiscard]] ShaderMacroDiagnosticsInsert Insert(
			const CompositeSetupTupleKeyView& a_key);
		[[nodiscard]] std::size_t Size() const noexcept;

	private:
		std::size_t _capacity = 0;
		std::vector<CompositeSetupTupleKey> _records;
	};

	[[nodiscard]] std::string FormatLightRuntimeHalfLine(
		const LightSetupTupleKeyView& a_key);
	[[nodiscard]] std::string FormatCompositeRuntimeHalfLine(
		const CompositeSetupTupleKeyView& a_key);
}
