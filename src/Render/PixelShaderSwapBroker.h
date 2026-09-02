#pragma once

#include "Utils/CSSha1.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <d3d11.h>

#include <cstddef>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cs::engine
{
	enum class ShaderStage : std::uint8_t
	{
		kVertex,
		kPixel,
		kCompute,
		kCount
	};

	using ShaderStageMask = std::uint32_t;

	constexpr ShaderStageMask ShaderStageBit(ShaderStage a_stage) noexcept
	{
		return ShaderStageMask{ 1 } << static_cast<unsigned>(a_stage);
	}

	class ShaderVariantId
	{
	public:
		constexpr ShaderVariantId() noexcept = default;
		explicit constexpr ShaderVariantId(std::uint32_t a_value) noexcept :
			_value(a_value)
		{}

		[[nodiscard]] constexpr std::uint32_t Value() const noexcept
		{
			return _value;
		}

		auto operator<=>(const ShaderVariantId&) const = default;

	private:
		std::uint32_t _value = 0;
	};

	struct ShaderVariantKey
	{
		std::string subclass;
		ShaderStage stage = ShaderStage::kPixel;
		ShaderVariantId id;
	};

	struct ShaderVariantKeyView
	{
		std::string_view subclass;
		ShaderStage stage = ShaderStage::kPixel;
		ShaderVariantId id;

		auto operator<=>(const ShaderVariantKeyView&) const = default;
	};

	inline ShaderVariantKeyView ViewShaderVariantKey(
		const ShaderVariantKey& a_key) noexcept
	{
		return { a_key.subclass, a_key.stage, a_key.id };
	}

	inline ShaderVariantKey OwnShaderVariantKey(ShaderVariantKeyView a_key)
	{
		return {
			std::string(a_key.subclass),
			a_key.stage,
			a_key.id
		};
	}

	struct PixelShaderSwapVariantKey
	{
		std::optional<ShaderVariantKey> variant;
		std::optional<sha1::Sha1Result> expectedStockSha1;
		std::size_t routeGroup = 0;
		std::size_t replacementIndex = 0;
		ShaderStage stage = ShaderStage::kPixel;
	};

	enum class PixelShaderSwapSelectionKind : std::uint8_t
	{
		kNoMatch,
		kUnmappedVariant,
		kSelected,
		kHashMismatch
	};

	struct PixelShaderSwapSelection
	{
		PixelShaderSwapSelectionKind kind =
			PixelShaderSwapSelectionKind::kNoMatch;
		std::size_t routeIndex = 0;
		std::size_t replacementIndex = 0;
		bool usedHashFallback = false;
	};

	PixelShaderSwapSelection SelectPixelShaderSwapVariant(
		std::span<const PixelShaderSwapVariantKey> a_variants,
		std::optional<ShaderVariantKeyView> a_variant,
		const sha1::Sha1Result& a_stockSha1,
		ShaderStage a_stage = ShaderStage::kPixel) noexcept;
	bool ShaderVariantKeysConflict(
		ShaderVariantKeyView a_left,
		ShaderVariantKeyView a_right) noexcept;
	bool ShouldSubstitutePixelShader(
		PixelShaderSwapSelectionKind a_selection,
		bool a_replacementReady) noexcept;

	struct ShaderSwapRequest
	{
		ID3D11Device* device = nullptr;
		ID3D11ClassLinkage* linkage = nullptr;
		const void* bytecode = nullptr;
		std::size_t bytecodeLength = 0;
		ShaderStage stage = ShaderStage::kPixel;
		std::optional<ShaderVariantKeyView> variant;
		sha1::Sha1Result stockSha1;
		ID3D11DeviceChild* stockOutput = nullptr;
		ID3D11DeviceChild** output = nullptr;
	};

	struct PixelShaderRuntimeRoute
	{
		std::string_view subclass;
		ShaderStage stage = ShaderStage::kPixel;
		std::uint32_t rawTechnique = 0;
		std::optional<ShaderVariantId> pluginResolvedPsid;
		std::optional<bool> tiledLighting;
	};

	enum class ShaderSwapResolverResult : std::uint8_t
	{
		kNoMatch,
		kKeepStock,
		kReplaced
	};

	using ShaderSwapResolver = ShaderSwapResolverResult (*)(
		const ShaderSwapRequest& a_request) noexcept;

	struct PixelShaderSwapResolverRegistration
	{
		ShaderSwapResolver resolver = nullptr;
		int priority = 0;
		ShaderStageMask stages = ShaderStageBit(ShaderStage::kPixel);
	};

	inline constexpr int kEarlyResolverPriority = -100;
	inline constexpr int kHlslReplacementResolverPriority = 0;

	using CreateShaderFunction = HRESULT (STDMETHODCALLTYPE *)(
		ID3D11Device*,
		const void*,
		SIZE_T,
		ID3D11ClassLinkage*,
		ID3D11DeviceChild**);

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
		ID3D11DeviceChild** a_output) noexcept;

	struct PixelShaderResolverRegistryIdentity
	{
		std::uint64_t registrationGeneration = 0;
		int priority = 0;

		auto operator<=>(const PixelShaderResolverRegistryIdentity&) const = default;
	};

	class PixelShaderResolverRegistryModel
	{
	public:
		std::uint64_t Register(int a_priority);
		bool Unregister(std::uint64_t a_registrationGeneration) noexcept;
		[[nodiscard]] std::uint64_t Generation() const noexcept;
		[[nodiscard]] std::span<const PixelShaderResolverRegistryIdentity>
			Identities() const noexcept;

	private:
		std::uint64_t _generation = 0;
		std::vector<PixelShaderResolverRegistryIdentity> _identities;
	};

	std::string BuildPixelShaderResolverRegistryDescriptor(
		std::span<const PixelShaderResolverRegistryIdentity> a_identities);

	void SetPixelShaderSwapBrokerDevice(ID3D11Device* a_device);
	bool RegisterPixelShaderSwapResolver(ShaderSwapResolver a_resolver);
	bool RegisterPixelShaderSwapResolver(
		PixelShaderSwapResolverRegistration a_registration);
	std::uintptr_t PixelShaderSwapBrokerCreateHookAddress() noexcept;
	bool PixelShaderSwapBrokerHooksInstalled() noexcept;
	bool PixelShaderBrokerBypassActive() noexcept;

	class ScopedPixelShaderBrokerBypass
	{
	public:
		ScopedPixelShaderBrokerBypass() noexcept;
		~ScopedPixelShaderBrokerBypass() noexcept;

		ScopedPixelShaderBrokerBypass(const ScopedPixelShaderBrokerBypass&) = delete;
		ScopedPixelShaderBrokerBypass(ScopedPixelShaderBrokerBypass&&) = delete;
		ScopedPixelShaderBrokerBypass& operator=(const ScopedPixelShaderBrokerBypass&) = delete;
		ScopedPixelShaderBrokerBypass& operator=(ScopedPixelShaderBrokerBypass&&) = delete;
	};
}
