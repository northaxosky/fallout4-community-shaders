#include "Render/PixelShaderSwapBroker.h"

#include <algorithm>
#include <array>

namespace cs::engine
{
	namespace
	{
		constexpr std::size_t kMaxObserverInvocations = 8;
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

	HRESULT ExecutePixelShaderSwapPipeline(
		CreatePixelShaderFunction a_original,
		std::span<const PixelShaderSwapObserver> a_observers,
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

		std::array<PixelShaderSwapObserverInvocation,
			kMaxObserverInvocations> invocations{};
		const auto observerCount =
			std::min(a_observers.size(), invocations.size());
		for (std::size_t index = 0; index < observerCount; ++index) {
			invocations[index] = BeginPixelShaderSwapObserver(
				a_observers[index],
				a_bytecode,
				a_bytecodeLength);
		}

		const HRESULT result = a_original(
			a_device,
			a_bytecode,
			a_bytecodeLength,
			a_linkage,
			a_output);
		ID3D11PixelShader* stockOutput = a_output ? *a_output : nullptr;
		const bool canResolve = SUCCEEDED(result)
			&& stockOutput
			&& a_bytecode
			&& a_bytecodeLength != 0;
		bool resolverInvoked = false;
		bool resolverReportedReplacement = false;
		if (canResolve) {
			const auto stockSha1 =
				sha1::Sha1Compute(a_bytecode, a_bytecodeLength);
			for (std::size_t index = 0; index < observerCount; ++index) {
				const auto& observer = a_observers[index];
				if (invocations[index].active
					&& observer.observeOriginal) {
					observer.observeOriginal(
						invocations[index].token,
						stockSha1,
						stockOutput);
				}
			}

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
				resolverInvoked = true;
				const auto resolution = registration.resolver(request);
				if (resolution
					== PixelShaderSwapResolverResult::kReplaced) {
					resolverReportedReplacement = true;
					break;
				}
				if (resolution
					== PixelShaderSwapResolverResult::kKeepStock) {
					break;
				}
			}
		}

		const auto completion = ClassifyPixelShaderSwapCompletion(
			static_cast<std::int32_t>(result),
			a_output != nullptr,
			stockOutput,
			resolverInvoked,
			resolverReportedReplacement,
			a_output ? *a_output : nullptr);
		for (std::size_t index = 0; index < observerCount; ++index)
			CompletePixelShaderSwapObserver(invocations[index], completion);
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
