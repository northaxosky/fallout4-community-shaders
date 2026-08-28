#include "Utils/ShaderCache/ShaderRecipe.h"

#include "Utils/ShaderCache/ByteCodec.h"

#include <algorithm>
#include <d3dcompiler.h>
#include <system_error>

static_assert(
	cs::shader_cache::kCachedOptimizedFlags1
		== (D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3
			| D3DCOMPILE_SKIP_VALIDATION),
	"cached bytecode is only interchangeable under the flags it was compiled with");

namespace cs::shader_cache
{
	namespace
	{
		constexpr std::string_view kRecipeSchema      = "FO4CS.shader-recipe";
		constexpr std::uint32_t    kRecipeSchemaVersion = 1;
		constexpr std::string_view kDependencySchema  = "FO4CS.shader-recipe.dependency";
	}

	const char* DescribeStage(ShaderCacheStage a_stage) noexcept
	{
		switch (a_stage) {
		case ShaderCacheStage::kVertex:
			return "vs";
		case ShaderCacheStage::kPixel:
			return "ps";
		case ShaderCacheStage::kCompute:
			return "cs";
		}
		return "??";
	}

	bool IsKnownStage(std::uint8_t a_stage) noexcept
	{
		return a_stage == static_cast<std::uint8_t>(ShaderCacheStage::kVertex)
			|| a_stage == static_cast<std::uint8_t>(ShaderCacheStage::kPixel)
			|| a_stage == static_cast<std::uint8_t>(ShaderCacheStage::kCompute);
	}

	std::string EncodeLocator(const std::filesystem::path& a_path)
	{
		std::error_code       error;
		std::filesystem::path resolved = std::filesystem::weakly_canonical(a_path, error);
		if (error || resolved.empty()) {
			resolved = std::filesystem::absolute(a_path, error);
			if (error)
				resolved = a_path;
		}

		// unencodable paths only cost a cache miss
		try {
			const auto  encoded = resolved.lexically_normal().u8string();
			std::string result(encoded.size(), '\0');
			std::ranges::transform(
				encoded,
				result.begin(),
				[](char8_t a_character) { return static_cast<char>(a_character); });
			return result;
		} catch (...) {
			return {};
		}
	}

	std::filesystem::path DecodeLocator(std::string_view a_locator)
	{
		const auto* first = reinterpret_cast<const char8_t*>(a_locator.data());
		return std::filesystem::path(first, first + a_locator.size());
	}

	std::vector<std::uint8_t> EncodeShaderRecipe(
		const ShaderRecipe&     a_recipe,
		const CompilerIdentity& a_identity)
	{
		std::vector<std::uint8_t> bytes;
		ByteWriter                writer(bytes);
		writer.Text(kRecipeSchema);
		writer.U32(kRecipeSchemaVersion);
		writer.Text(EncodeLocator(a_recipe.source));
		writer.U32(static_cast<std::uint32_t>(a_recipe.includeRoots.size()));
		for (const auto& root : a_recipe.includeRoots)
			writer.Text(EncodeLocator(root));
		writer.U32(static_cast<std::uint32_t>(a_recipe.defines.size()));
		for (const auto& [name, value] : a_recipe.defines) {
			writer.Text(name);
			writer.Text(value);
		}
		writer.Text(a_recipe.entryPoint);
		writer.Text(a_recipe.profile);
		writer.U8(static_cast<std::uint8_t>(a_recipe.stage));
		writer.U32(a_recipe.flags1);
		writer.U32(a_recipe.flags2);
		writer.Text(EncodeLocator(a_identity.modulePath));
		writer.U64(a_identity.moduleLength);
		writer.Digest(a_identity.moduleDigest);
		return bytes;
	}

	sha256::Sha256Result ComputeLogicalDigest(
		std::span<const std::uint8_t> a_recipeBytes) noexcept
	{
		return sha256::Sha256Compute(a_recipeBytes.data(), a_recipeBytes.size());
	}

	sha256::Sha256Result ComputeFullRecipeDigest(
		std::span<const std::uint8_t> a_recipeBytes,
		const sha256::Sha256Result&   a_dependencyDigest)
	{
		std::vector<std::uint8_t> bytes(a_recipeBytes.begin(), a_recipeBytes.end());
		ByteWriter                writer(bytes);
		writer.Text(kDependencySchema);
		writer.Digest(a_dependencyDigest);
		return sha256::Sha256Compute(bytes.data(), bytes.size());
	}
}
