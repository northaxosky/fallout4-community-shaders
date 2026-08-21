#pragma once

#include "Utils/CSSha256.h"
#include "Utils/ShaderCache/CompilerIdentity.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cs::shader_cache
{
	enum class ShaderCacheStage : std::uint8_t
	{
		kVertex = 0,
		kPixel  = 1
	};

	// strict optimized flags; verified against SDK headers
	inline constexpr std::uint32_t kStrictOptimizedFlags1 = 0x00008800u;

	struct ShaderRecipe
	{
		std::filesystem::path                           source;
		std::vector<std::filesystem::path>              includeRoots;
		std::vector<std::pair<std::string, std::string>> defines;
		std::string                                     entryPoint;
		std::string                                     profile;
		ShaderCacheStage                                stage  = ShaderCacheStage::kPixel;
		std::uint32_t                                   flags1 = kStrictOptimizedFlags1;
		std::uint32_t                                   flags2 = 0;
	};

	const char* DescribeStage(ShaderCacheStage a_stage) noexcept;
	bool        IsKnownStage(std::uint8_t a_stage) noexcept;

	// canonical UTF-8 keeps path hashes stable
	std::string EncodeLocator(const std::filesystem::path& a_path);
	// avoid interpreting UTF-8 through the ANSI code page
	std::filesystem::path DecodeLocator(std::string_view a_locator);

	std::vector<std::uint8_t> EncodeShaderRecipe(
		const ShaderRecipe&     a_recipe,
		const CompilerIdentity& a_identity);
	sha256::Sha256Result ComputeLogicalDigest(
		std::span<const std::uint8_t> a_recipeBytes) noexcept;
	sha256::Sha256Result ComputeFullRecipeDigest(
		std::span<const std::uint8_t> a_recipeBytes,
		const sha256::Sha256Result&   a_dependencyDigest);
}
