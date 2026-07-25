#pragma once

#include "Utils/CSSha1.h"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct ID3D11Device;
struct ID3D11PixelShader;

namespace cs::engine
{
	enum class ShaderStage : std::uint8_t
	{
		kVertex,
		kPixel
	};

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
		const sha1::Sha1Result& a_stockSha1) noexcept;
	bool ShaderVariantKeysConflict(
		ShaderVariantKeyView a_left,
		ShaderVariantKeyView a_right) noexcept;
	bool ShouldSubstitutePixelShader(
		PixelShaderSwapSelectionKind a_selection,
		bool a_replacementReady) noexcept;

	// A successful resolver replaces *a_out with net refcount one.
	using PixelShaderSwapResolver = bool (*)(
		const void* a_bytecode,
		std::size_t a_bytecodeLength,
		std::optional<ShaderVariantKeyView> a_variant,
		const sha1::Sha1Result& a_sha,
		ID3D11PixelShader** a_out) noexcept;

	struct PixelShaderSwapCompletion
	{
		std::int32_t originalResult = 0;
		bool outputRequested = false;
		ID3D11PixelShader* stockOutput = nullptr;
		bool resolverInvoked = false;
		bool resolverReportedReplacement = false;
		ID3D11PixelShader* finalOutput = nullptr;
		bool finalIsStock = false;
		bool finalIsReplacement = false;
		bool finalIsNull = true;
	};

	using PreparePixelShaderObserver = void* (*)(
		const void* a_bytecode, std::size_t a_bytecodeLength) noexcept;
	using BeginPixelShaderObserverAdmission = bool (*)() noexcept;
	using EndPixelShaderObserverAdmission = void (*)() noexcept;
	using OriginalPixelShaderObserver = void (*)(
		void* a_token, const sha1::Sha1Result& a_sha,
		ID3D11PixelShader* a_shader) noexcept;
	using CompletePixelShaderObserver = void (*)(
		void* a_token, const PixelShaderSwapCompletion& a_completion) noexcept;

	struct PixelShaderSwapObserver
	{
		BeginPixelShaderObserverAdmission beginAdmission = nullptr;
		EndPixelShaderObserverAdmission endAdmission = nullptr;
		PreparePixelShaderObserver prepare = nullptr;
		OriginalPixelShaderObserver observeOriginal = nullptr;
		CompletePixelShaderObserver complete = nullptr;
	};

	struct PixelShaderSwapObserverInvocation
	{
		PixelShaderSwapObserver observer{};
		void* token = nullptr;
		bool active = false;
		bool admitted = false;
	};

	PixelShaderSwapObserverInvocation BeginPixelShaderSwapObserver(
		PixelShaderSwapObserver a_observer,
		const void* a_bytecode,
		std::size_t a_bytecodeLength) noexcept;
	void CompletePixelShaderSwapObserver(
		PixelShaderSwapObserverInvocation& a_invocation,
		const PixelShaderSwapCompletion& a_completion) noexcept;

	PixelShaderSwapCompletion ClassifyPixelShaderSwapCompletion(
		std::int32_t a_originalResult,
		bool a_outputRequested,
		ID3D11PixelShader* a_stockOutput,
		bool a_resolverInvoked,
		bool a_resolverReportedReplacement,
		ID3D11PixelShader* a_finalOutput) noexcept;

	void SetPixelShaderSwapBrokerDevice(ID3D11Device* a_device);
	bool RegisterPixelShaderSwapResolver(PixelShaderSwapResolver a_resolver);
	bool RegisterPixelShaderSwapObserver(PixelShaderSwapObserver a_observer);
	bool PixelShaderSwapBrokerHooksInstalled() noexcept;

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
