#include "Render/PixelShaderSwapBroker.h"

#include <algorithm>

namespace cs::engine
{
	namespace
	{
		thread_local unsigned g_bypassDepth = 0;

		bool Sha1Equals(
			const sha1::Sha1Result& a_left,
			const sha1::Sha1Result& a_right) noexcept
		{
			return a_left.bytes == a_right.bytes;
		}
	}

	PixelShaderSwapSelection SelectPixelShaderSwapVariant(
		std::span<const PixelShaderSwapVariantKey> a_variants,
		std::optional<ShaderVariantKeyView> a_variant,
		const sha1::Sha1Result& a_stockSha1) noexcept
	{
		if (a_variant) {
			for (std::size_t i = 0; i < a_variants.size(); ++i) {
				const auto& variant = a_variants[i];
				if (!variant.variant
					|| !ShaderVariantKeysConflict(
						ViewShaderVariantKey(*variant.variant),
						*a_variant)) {
					continue;
				}

				if (!variant.expectedStockSha1
					|| !Sha1Equals(
						*variant.expectedStockSha1,
						a_stockSha1)) {
					return {
						.kind =
							PixelShaderSwapSelectionKind::kHashMismatch,
						.routeIndex = i,
						.replacementIndex =
							variant.replacementIndex
					};
				}
				return {
					.kind = PixelShaderSwapSelectionKind::kSelected,
					.routeIndex = i,
					.replacementIndex = variant.replacementIndex
				};
			}

			for (std::size_t i = 0; i < a_variants.size(); ++i) {
				const auto& variant = a_variants[i];
				if (variant.variant
					|| !variant.expectedStockSha1
					|| !Sha1Equals(
						*variant.expectedStockSha1,
						a_stockSha1)) {
					continue;
				}

				const bool groupHasVariantRoutes =
					std::ranges::any_of(
						a_variants,
						[&variant, &a_variant](
							const PixelShaderSwapVariantKey& a_candidate) {
							return a_candidate.routeGroup
									== variant.routeGroup
								&& a_candidate.variant
								&& a_candidate.variant->subclass
									== a_variant->subclass
								&& a_candidate.variant->stage
									== a_variant->stage;
						});
				if (!groupHasVariantRoutes) {
					return {
						.kind = PixelShaderSwapSelectionKind::kSelected,
						.routeIndex = i,
						.replacementIndex =
							variant.replacementIndex,
						.usedHashFallback = true
					};
				}
			}
			return {
				.kind =
					PixelShaderSwapSelectionKind::kUnmappedVariant
			};
		}

		for (std::size_t i = 0; i < a_variants.size(); ++i) {
			const auto& expected = a_variants[i].expectedStockSha1;
			if (expected && Sha1Equals(*expected, a_stockSha1)) {
				return {
					.kind = PixelShaderSwapSelectionKind::kSelected,
					.routeIndex = i,
					.replacementIndex =
						a_variants[i].replacementIndex,
					.usedHashFallback = true
				};
			}
		}
		return {};
	}

	bool ShaderVariantKeysConflict(
		ShaderVariantKeyView a_left,
		ShaderVariantKeyView a_right) noexcept
	{
		return a_left.subclass == a_right.subclass
			&& a_left.stage == a_right.stage
			&& a_left.id == a_right.id;
	}

	bool ShouldSubstitutePixelShader(
		PixelShaderSwapSelectionKind a_selection,
		bool a_replacementReady) noexcept
	{
		return a_selection == PixelShaderSwapSelectionKind::kSelected
			&& a_replacementReady;
	}

	HRESULT ExecutePixelShaderSwapPipeline(
		CreatePixelShaderFunction a_original,
		std::span<const PixelShaderSwapResolverRegistration> a_resolvers,
		std::optional<ShaderVariantKeyView> a_variant,
		bool a_bypass,
		ID3D11Device* a_device,
		const void* a_bytecode,
		SIZE_T a_bytecodeLength,
		ID3D11ClassLinkage* a_linkage,
		ID3D11PixelShader** a_output) noexcept
	{
		if (!a_original)
			return E_POINTER;
		if (a_bypass) {
			return a_original(
				a_device,
				a_bytecode,
				a_bytecodeLength,
				a_linkage,
				a_output);
		}

		const bool inputHashable =
			a_bytecode != nullptr && a_bytecodeLength != 0;

		const HRESULT result = a_original(
			a_device,
			a_bytecode,
			a_bytecodeLength,
			a_linkage,
			a_output);
		sha1::Sha1Result postInputSha1{};
		if (inputHashable) {
			postInputSha1 =
				sha1::Sha1Compute(a_bytecode, a_bytecodeLength);
		}
		ID3D11PixelShader* stockOutput = a_output ? *a_output : nullptr;
		const bool canResolve = SUCCEEDED(result)
			&& stockOutput
			&& a_bytecode
			&& a_bytecodeLength != 0;
		if (canResolve) {
			const auto& stockSha1 = postInputSha1;
			const PixelShaderSwapRequest request{
				.device = a_device,
				.linkage = a_linkage,
				.bytecode = a_bytecode,
				.bytecodeLength = a_bytecodeLength,
				.variant = a_variant,
				.stockSha1 = stockSha1,
				.stockOutput = stockOutput,
				.output = a_output
			};
			for (const auto& registration : a_resolvers) {
				if (!registration.resolver)
					continue;
				const auto resolution = registration.resolver(request);
				if (resolution
					== PixelShaderSwapResolverResult::kReplaced) {
					break;
				}
				if (resolution
					== PixelShaderSwapResolverResult::kKeepStock) {
					break;
				}
			}
		}

		return result;
	}

	std::uint64_t PixelShaderResolverRegistryModel::Register(
		int a_priority)
	{
		const auto generation = _generation + 1;
		_identities.push_back({
			.registrationGeneration = generation,
			.priority = a_priority
		});
		_generation = generation;
		return generation;
	}

	bool PixelShaderResolverRegistryModel::Unregister(
		std::uint64_t a_registrationGeneration) noexcept
	{
		const auto found = std::ranges::find(
			_identities,
			a_registrationGeneration,
			&PixelShaderResolverRegistryIdentity::registrationGeneration);
		if (found == _identities.end())
			return false;
		_identities.erase(found);
		++_generation;
		return true;
	}

	std::uint64_t PixelShaderResolverRegistryModel::Generation() const noexcept
	{
		return _generation;
	}

	std::span<const PixelShaderResolverRegistryIdentity>
		PixelShaderResolverRegistryModel::Identities() const noexcept
	{
		return _identities;
	}

	std::string BuildPixelShaderResolverRegistryDescriptor(
		std::span<const PixelShaderResolverRegistryIdentity> a_identities)
	{
		std::string result = "{\"resolvers\":[";
		for (std::size_t index = 0; index < a_identities.size(); ++index) {
			if (index != 0)
				result.push_back(',');
			result += "{\"priority\":"
				+ std::to_string(a_identities[index].priority)
				+ ",\"registration_generation\":"
				+ std::to_string(
					a_identities[index].registrationGeneration)
				+ '}';
		}
		result +=
			"],\"schema\":\"fo4cs.broker-resolver-registry\","
			"\"schema_version\":1}\n";
		return result;
	}

	bool PixelShaderBrokerBypassActive() noexcept
	{
		return g_bypassDepth != 0;
	}

	ScopedPixelShaderBrokerBypass::ScopedPixelShaderBrokerBypass() noexcept
	{
		++g_bypassDepth;
	}

	ScopedPixelShaderBrokerBypass::~ScopedPixelShaderBrokerBypass() noexcept
	{
		--g_bypassDepth;
	}
}
