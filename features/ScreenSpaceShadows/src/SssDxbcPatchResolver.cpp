#include "SssDxbcPatchResolver.h"

#include "Utils/CSSha256.h"

namespace cs::features::sss_dxbc_patch
{
	engine::PixelShaderSwapResolverResult ResolveRequest(
		const engine::dxbc_patch::Artifact& a_artifact,
		bool a_runtimeExact,
		bool a_dispatchPublished,
		bool a_bindingReady,
		AtomicCounters& a_counters,
		const engine::PixelShaderSwapRequest& a_request,
		CreatePatchedPixelShaderFunction a_create) noexcept
	{
		if (!a_request.variant)
			return engine::PixelShaderSwapResolverResult::kNoMatch;
		const auto* route = engine::dxbc_patch::FindCandidateRoute(
			a_artifact,
			*a_request.variant);
		if (!route)
			return engine::PixelShaderSwapResolverResult::kNoMatch;

		a_counters.candidateSeen.fetch_add(1, std::memory_order_relaxed);
		if (!a_runtimeExact
			|| !a_dispatchPublished
			|| !a_bindingReady) {
			a_counters.unknownStockFallback.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		if (a_request.linkage) {
			a_counters.unknownStockFallback.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		if (route->stock.length != a_request.bytecodeLength
			|| route->stock.sha1.bytes != a_request.stockSha1.bytes) {
			a_counters.hashMismatch.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}

		const auto stockSha256 = sha256::Sha256Compute(
			a_request.bytecode,
			a_request.bytecodeLength);
		if (!engine::dxbc_patch::StockMatches(
				route->stock,
				a_request.bytecodeLength,
				a_request.stockSha1,
				stockSha256)) {
			a_counters.hashMismatch.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		if (route->status
				!= engine::dxbc_patch::CandidateStatus::kPass
			|| !route->planIndex
			|| *route->planIndex >= a_artifact.plans.size()) {
			a_counters.unknownStockFallback.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		a_counters.exactRouteAdmitted.fetch_add(
			1,
			std::memory_order_relaxed);

		const auto planIndex = *route->planIndex;
		const auto& plan = a_artifact.plans[planIndex];
		const auto stockBytes = std::span<const std::byte>(
			static_cast<const std::byte*>(a_request.bytecode),
			a_request.bytecodeLength);
		auto patch = engine::dxbc_patch::BuildPatchedDxbc(
			stockBytes,
			plan);
		switch (patch.status) {
		case engine::dxbc_patch::PatchBuildStatus::kSuccess:
			break;
		case engine::dxbc_patch::PatchBuildStatus::kStockMismatch:
			a_counters.hashMismatch.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		case engine::dxbc_patch::PatchBuildStatus::kPreimageMismatch:
			a_counters.preimageMismatch.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		case engine::dxbc_patch::PatchBuildStatus::kMalformedDxbc:
		case engine::dxbc_patch::PatchBuildStatus::kOutputMismatch:
		case engine::dxbc_patch::PatchBuildStatus::kAllocationFailure:
			a_counters.checksumHashMismatch.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		a_counters.patchBuilt.fetch_add(1, std::memory_order_relaxed);

		const auto publication = PublishPatchedPixelShader(
			a_request,
			plan,
			planIndex,
			patch.bytecode,
			a_create);
		switch (publication) {
		case PublishStatus::kAccepted:
			a_counters.createPsAccepted.fetch_add(
				1,
				std::memory_order_relaxed);
			a_counters.identityPublished.fetch_add(
				1,
				std::memory_order_relaxed);
			return engine::PixelShaderSwapResolverResult::kReplaced;
		case PublishStatus::kIdentityRejected:
			a_counters.createPsAccepted.fetch_add(
				1,
				std::memory_order_relaxed);
			break;
		case PublishStatus::kInvalidRequest:
		case PublishStatus::kCreateRejected:
			a_counters.createPsRejected.fetch_add(
				1,
				std::memory_order_relaxed);
			break;
		}
		return engine::PixelShaderSwapResolverResult::kKeepStock;
	}

	TelemetrySnapshot SnapshotCounters(
		const AtomicCounters& a_counters) noexcept
	{
		return {
			.candidateSeen = a_counters.candidateSeen.load(
				std::memory_order_relaxed),
			.exactRouteAdmitted = a_counters.exactRouteAdmitted.load(
				std::memory_order_relaxed),
			.hashMismatch = a_counters.hashMismatch.load(
				std::memory_order_relaxed),
			.preimageMismatch = a_counters.preimageMismatch.load(
				std::memory_order_relaxed),
			.patchBuilt = a_counters.patchBuilt.load(
				std::memory_order_relaxed),
			.checksumHashMismatch =
				a_counters.checksumHashMismatch.load(
					std::memory_order_relaxed),
			.createPsAccepted = a_counters.createPsAccepted.load(
				std::memory_order_relaxed),
			.createPsRejected = a_counters.createPsRejected.load(
				std::memory_order_relaxed),
			.identityPublished = a_counters.identityPublished.load(
				std::memory_order_relaxed),
			.patchedDrawMatched = a_counters.patchedDrawMatched.load(
				std::memory_order_relaxed),
			.stockShaderFallback = a_counters.stockShaderFallback.load(
				std::memory_order_relaxed),
			.stockShaderFallbackFailed =
				a_counters.stockShaderFallbackFailed.load(
					std::memory_order_relaxed),
			.unknownStockFallback =
				a_counters.unknownStockFallback.load(
					std::memory_order_relaxed)
		};
	}
}
