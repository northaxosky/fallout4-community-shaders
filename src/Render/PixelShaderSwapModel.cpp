#include "Render/PixelShaderSwapBroker.h"

#include <algorithm>

namespace cs::engine
{
	namespace
	{
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

	PixelShaderSwapObserverInvocation BeginPixelShaderSwapObserver(
		PixelShaderSwapObserver a_observer,
		const void* a_bytecode,
		std::size_t a_bytecodeLength) noexcept
	{
		PixelShaderSwapObserverInvocation invocation;
		invocation.observer = a_observer;
		if (a_observer.beginAdmission) {
			invocation.admitted = a_observer.beginAdmission();
			if (!invocation.admitted)
				return invocation;
		}
		invocation.active = true;
		if (a_observer.prepare) {
			invocation.token =
				a_observer.prepare(a_bytecode, a_bytecodeLength);
		}
		return invocation;
	}

	void CompletePixelShaderSwapObserver(
		PixelShaderSwapObserverInvocation& a_invocation,
		const PixelShaderSwapCompletion& a_completion) noexcept
	{
		if (a_invocation.active && a_invocation.observer.complete) {
			a_invocation.observer.complete(
				a_invocation.token, a_completion);
		}
		if (a_invocation.admitted
			&& a_invocation.observer.endAdmission) {
			a_invocation.observer.endAdmission();
		}
		a_invocation = {};
	}

	PixelShaderSwapCompletion ClassifyPixelShaderSwapCompletion(
		std::int32_t a_originalResult,
		bool a_outputRequested,
		ID3D11PixelShader* a_stockOutput,
		bool a_resolverInvoked,
		bool a_resolverReportedReplacement,
		ID3D11PixelShader* a_finalOutput) noexcept
	{
		PixelShaderSwapCompletion result;
		result.originalResult = a_originalResult;
		result.outputRequested = a_outputRequested;
		result.stockOutput = a_stockOutput;
		result.resolverInvoked = a_resolverInvoked;
		result.resolverReportedReplacement = a_resolverReportedReplacement;
		result.finalOutput = a_finalOutput;
		const bool originalSucceeded = a_originalResult >= 0;
		const bool usableFinal = originalSucceeded
			&& a_outputRequested
			&& a_finalOutput != nullptr;
		result.finalIsNull = a_outputRequested && a_finalOutput == nullptr;
		result.finalIsStock = usableFinal
			&& a_finalOutput == a_stockOutput;
		result.finalIsReplacement = usableFinal
			&& a_stockOutput != nullptr
			&& a_finalOutput != a_stockOutput;
		return result;
	}
}
