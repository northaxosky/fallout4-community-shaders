#include "Render/ShaderInjectionVariantData.h"

#include "Render/ShaderInjectionEmbeddedData.h"
#include "Render/ShaderInjectionVariantFactory.h"

#include <stdexcept>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

namespace cs::engine
{
	namespace
	{
		[[noreturn]] void ThrowDataError(
			std::string_view a_source,
			std::string_view a_message,
			std::size_t a_index)
		{
			throw std::runtime_error(
				std::string(a_source)
				+ ": "
				+ std::string(a_message)
				+ " at variant "
				+ std::to_string(a_index));
		}

		ShaderStage ParseStage(
			std::string_view a_stage,
			std::string_view a_source,
			std::size_t a_index)
		{
			if (a_stage == "vertex")
				return ShaderStage::kVertex;
			if (a_stage == "pixel")
				return ShaderStage::kPixel;
			if (a_stage == "compute")
				return ShaderStage::kCompute;
			ThrowDataError(a_source, "unknown shader stage", a_index);
		}

		std::string ReadString(
			const toml::table& a_table,
			std::string_view a_key,
			std::string_view a_source,
			std::size_t a_index)
		{
			const auto value = a_table[a_key].value<std::string>();
			if (!value)
				ThrowDataError(a_source, "missing string field", a_index);
			return *value;
		}

		void ValidateVariantKeys(
			const toml::table& a_table,
			std::string_view a_source,
			std::size_t a_index)
		{
			for (const auto& entry : a_table) {
				const auto name = entry.first.str();
				if (name != "target"
					&& name != "name"
					&& name != "stock_sha1"
					&& name != "stage"
					&& name != "defines") {
					ThrowDataError(a_source, "unknown field", a_index);
				}
			}
		}

		ShaderInjectionDefines ParseDefines(
			const toml::table& a_table,
			std::string_view a_source,
			std::size_t a_index)
		{
			const auto* definitions = a_table["defines"].as_array();
			if (!definitions)
				ThrowDataError(a_source, "missing defines array", a_index);

			ShaderInjectionDefines result;
			for (const auto& definitionNode : *definitions) {
				const auto* definition = definitionNode.as_array();
				if (!definition || definition->size() != 2)
					ThrowDataError(a_source, "invalid define pair", a_index);
				const auto name = (*definition)[0].value<std::string>();
				const auto value = (*definition)[1].value<std::string>();
				if (!name || !value || name->empty())
					ThrowDataError(a_source, "invalid define value", a_index);
				if (!result.emplace(*name, *value).second)
					ThrowDataError(a_source, "duplicate define", a_index);
			}
			return result;
		}

		void AppendShaderReplacementVariants(
			std::vector<ShaderReplacementVariantRegistration>& a_variants,
			std::string_view a_document,
			std::string_view a_source,
			std::size_t a_expectedCount)
		{
			const auto root = toml::parse(a_document, a_source);
			for (const auto& entry : root) {
				const auto name = entry.first.str();
				if (name != "format_version" && name != "variants")
					throw std::runtime_error(
						std::string(a_source) + ": unknown root field");
			}
			if (root["format_version"].value<std::int64_t>() != 1)
				throw std::runtime_error(
					std::string(a_source) + ": unsupported format version");

			const auto* variants = root["variants"].as_array();
			if (!variants || variants->size() != a_expectedCount)
				throw std::runtime_error(
					std::string(a_source) + ": route count mismatch");

			a_variants.reserve(a_variants.size() + variants->size());
			for (std::size_t index = 0; index < variants->size(); ++index) {
				const auto* row = (*variants)[index].as_table();
				if (!row)
					ThrowDataError(a_source, "variant is not a table", index);
				ValidateVariantKeys(*row, a_source, index);

				const auto targetName = ReadString(
					*row, "target", a_source, index);
				const auto* target = FindShaderInjectionTarget(targetName);
				if (!target)
					ThrowDataError(a_source, "unknown target", index);
				const auto stage = ParseStage(
					ReadString(*row, "stage", a_source, index),
					a_source,
					index);
				a_variants.push_back(MakeDefaultVariantRegistration(
					target->id,
					ReadString(*row, "name", a_source, index),
					{},
					ReadString(*row, "stock_sha1", a_source, index),
					ParseDefines(*row, a_source, index),
					stage));
			}
		}
	}

	void AppendBsdfFamilyShaderReplacementVariants(
		std::vector<ShaderReplacementVariantRegistration>& a_variants)
	{
		AppendShaderReplacementVariants(
			a_variants,
			embedded::BsdfShaderReplacementVariants(),
			"bsdf.toml",
			241);
	}

	void AppendStaticFamilyShaderReplacementVariants(
		std::vector<ShaderReplacementVariantRegistration>& a_variants)
	{
		AppendShaderReplacementVariants(
			a_variants,
			embedded::StaticFamilyShaderReplacementVariants(),
			"static_families.toml",
			90);
	}
}
