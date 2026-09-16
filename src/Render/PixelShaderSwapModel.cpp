#include "Render/PixelShaderSwapBroker.h"

#include <algorithm>

namespace cs::engine
{
	namespace
	{
		thread_local unsigned g_bypassDepth = 0;
	}

	HRESULT ExecuteShaderSwapPipeline(
		CreateShaderFunction a_original,
		std::span<const PixelShaderSwapResolverRegistration> a_resolvers,
		std::optional<ShaderVariantKeyView> a_variant,
		bool a_bypass,
		ShaderStage a_stage,
		ID3D11Device* a_device,
		const void* a_bytecode,
		SIZE_T a_bytecodeLength,
		ID3D11ClassLinkage* a_linkage,
		ID3D11DeviceChild** a_output) noexcept
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

		const HRESULT result = a_original(
			a_device,
			a_bytecode,
			a_bytecodeLength,
			a_linkage,
			a_output);
		ID3D11DeviceChild* stockOutput = a_output ? *a_output : nullptr;
		const bool canResolve = SUCCEEDED(result)
			&& stockOutput
			&& a_bytecode
			&& a_bytecodeLength != 0;
		if (canResolve) {
			const ShaderSwapRequest request{
				.device = a_device,
				.linkage = a_linkage,
				.bytecode = a_bytecode,
				.bytecodeLength = a_bytecodeLength,
				.stage = a_stage,
				.variant = a_variant,
				.stockOutput = stockOutput,
				.output = a_output
			};
			for (const auto& registration : a_resolvers) {
				if (!registration.resolver
					|| (registration.stages & ShaderStageBit(a_stage)) == 0) {
					continue;
				}
				const auto resolution = registration.resolver(request);
				if (resolution
					== ShaderSwapResolverResult::kReplaced) {
					break;
				}
				if (resolution
					== ShaderSwapResolverResult::kKeepStock) {
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
