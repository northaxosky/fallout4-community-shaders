#include "SssDxbcPatchResolver.h"

#include "Utils/CSSha256.h"

namespace cs::features::sss_dxbc_patch
{
	namespace
	{
		class CounterTransaction
		{
		public:
			explicit CounterTransaction(AtomicCounters& a_counters) noexcept :
				_counters(a_counters)
			{}

			~CounterTransaction() noexcept
			{
				_counters.Publish(delta);
			}

			CounterTransaction(const CounterTransaction&) = delete;
			CounterTransaction(CounterTransaction&&) = delete;
			CounterTransaction& operator=(const CounterTransaction&) = delete;
			CounterTransaction& operator=(CounterTransaction&&) = delete;

			CounterDelta delta;

		private:
			AtomicCounters& _counters;
		};

#ifdef FO4CS_SSS_DXBC_PATCH_TESTING
		std::atomic<SnapshotTestHook> g_snapshotTestHook;

		void InvokeSnapshotTestHook(SnapshotTestPoint a_point) noexcept
		{
			if (const auto hook =
					g_snapshotTestHook.load(std::memory_order_acquire)) {
				hook(a_point);
			}
		}
#endif
	}

#ifdef FO4CS_SSS_DXBC_PATCH_TESTING
	void SetSnapshotTestHook(SnapshotTestHook a_hook) noexcept
	{
		g_snapshotTestHook.store(a_hook, std::memory_order_release);
	}
#endif

	void AtomicCounters::Publish(const CounterDelta& a_delta) noexcept
	{
		auto sequence = _sequence.load(std::memory_order_seq_cst);
		while ((sequence & 1u) != 0
			|| !_sequence.compare_exchange_weak(
				sequence,
				sequence + 1,
				std::memory_order_seq_cst,
				std::memory_order_seq_cst)) {
			sequence = _sequence.load(std::memory_order_seq_cst);
		}

		const auto add =
			[](std::atomic_uint64_t& a_counter,
				std::uint64_t a_value) noexcept {
			if (a_value != 0) {
				a_counter.fetch_add(
					a_value,
					std::memory_order_seq_cst);
			}
		};
		add(_candidateSeen, a_delta.candidateSeen);
		add(_exactRouteAdmitted, a_delta.exactRouteAdmitted);
		add(_hashMismatch, a_delta.hashMismatch);
		add(_preimageMismatch, a_delta.preimageMismatch);
		add(_patchBuilt, a_delta.patchBuilt);
		add(_checksumHashMismatch, a_delta.checksumHashMismatch);
		add(_createPsAccepted, a_delta.createPsAccepted);
		add(_createPsRejected, a_delta.createPsRejected);
		add(_identityPublished, a_delta.identityPublished);
		add(_patchedDrawMatched, a_delta.patchedDrawMatched);
		add(_stockShaderFallback, a_delta.stockShaderFallback);
		add(
			_stockShaderFallbackFailed,
			a_delta.stockShaderFallbackFailed);
		add(_realMaskBinds, a_delta.realMaskBinds);
		add(_whiteFallbackBinds, a_delta.whiteFallbackBinds);
		add(_invariantViolations, a_delta.invariantViolations);
		add(_unknownStockFallback, a_delta.unknownStockFallback);

		_sequence.store(sequence + 2, std::memory_order_seq_cst);
	}

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

		CounterTransaction telemetry(a_counters);
		telemetry.delta.candidateSeen = 1;
		if (!a_runtimeExact
			|| !a_dispatchPublished
			|| !a_bindingReady) {
			telemetry.delta.unknownStockFallback = 1;
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		if (a_request.linkage) {
			telemetry.delta.unknownStockFallback = 1;
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		if (route->stock.length != a_request.bytecodeLength
			|| route->stock.sha1.bytes != a_request.stockSha1.bytes) {
			telemetry.delta.hashMismatch = 1;
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
			telemetry.delta.hashMismatch = 1;
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		if (route->status
				!= engine::dxbc_patch::CandidateStatus::kPass
			|| !route->planIndex
			|| *route->planIndex >= a_artifact.plans.size()) {
			telemetry.delta.unknownStockFallback = 1;
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		telemetry.delta.exactRouteAdmitted = 1;

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
			telemetry.delta.hashMismatch = 1;
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		case engine::dxbc_patch::PatchBuildStatus::kPreimageMismatch:
			telemetry.delta.preimageMismatch = 1;
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		case engine::dxbc_patch::PatchBuildStatus::kMalformedDxbc:
		case engine::dxbc_patch::PatchBuildStatus::kOutputMismatch:
		case engine::dxbc_patch::PatchBuildStatus::kAllocationFailure:
			telemetry.delta.checksumHashMismatch = 1;
			return engine::PixelShaderSwapResolverResult::kKeepStock;
		}
		telemetry.delta.patchBuilt = 1;

		const auto publication = PublishPatchedPixelShader(
			a_request,
			plan,
			planIndex,
			patch.bytecode,
			a_create);
		switch (publication) {
		case PublishStatus::kAccepted:
			telemetry.delta.createPsAccepted = 1;
			telemetry.delta.identityPublished = 1;
			return engine::PixelShaderSwapResolverResult::kReplaced;
		case PublishStatus::kIdentityRejected:
			telemetry.delta.createPsAccepted = 1;
			break;
		case PublishStatus::kInvalidRequest:
		case PublishStatus::kCreateRejected:
			telemetry.delta.createPsRejected = 1;
			break;
		}
		return engine::PixelShaderSwapResolverResult::kKeepStock;
	}

	TelemetrySnapshot SnapshotCounters(
		const AtomicCounters& a_counters) noexcept
	{
		while (true) {
			const auto sequence =
				a_counters._sequence.load(std::memory_order_seq_cst);
			if ((sequence & 1u) != 0)
				continue;

			TelemetrySnapshot snapshot;
			snapshot.candidateSeen =
				a_counters._candidateSeen.load(
					std::memory_order_seq_cst);
#ifdef FO4CS_SSS_DXBC_PATCH_TESTING
			InvokeSnapshotTestHook(SnapshotTestPoint::kCandidateSeen);
#endif
			snapshot.exactRouteAdmitted =
				a_counters._exactRouteAdmitted.load(
					std::memory_order_seq_cst);
			snapshot.hashMismatch =
				a_counters._hashMismatch.load(
					std::memory_order_seq_cst);
			snapshot.preimageMismatch =
				a_counters._preimageMismatch.load(
					std::memory_order_seq_cst);
			snapshot.patchBuilt =
				a_counters._patchBuilt.load(
					std::memory_order_seq_cst);
			snapshot.checksumHashMismatch =
				a_counters._checksumHashMismatch.load(
					std::memory_order_seq_cst);
			snapshot.createPsAccepted =
				a_counters._createPsAccepted.load(
					std::memory_order_seq_cst);
			snapshot.createPsRejected =
				a_counters._createPsRejected.load(
					std::memory_order_seq_cst);
			snapshot.identityPublished =
				a_counters._identityPublished.load(
					std::memory_order_seq_cst);
			snapshot.patchedDrawMatched =
				a_counters._patchedDrawMatched.load(
					std::memory_order_seq_cst);
#ifdef FO4CS_SSS_DXBC_PATCH_TESTING
			InvokeSnapshotTestHook(
				SnapshotTestPoint::kPatchedDrawMatched);
#endif
			snapshot.stockShaderFallback =
				a_counters._stockShaderFallback.load(
					std::memory_order_seq_cst);
			snapshot.stockShaderFallbackFailed =
				a_counters._stockShaderFallbackFailed.load(
					std::memory_order_seq_cst);
			snapshot.realMaskBinds =
				a_counters._realMaskBinds.load(
					std::memory_order_seq_cst);
			snapshot.whiteFallbackBinds =
				a_counters._whiteFallbackBinds.load(
					std::memory_order_seq_cst);
			snapshot.invariantViolations =
				a_counters._invariantViolations.load(
					std::memory_order_seq_cst);
			snapshot.unknownStockFallback =
				a_counters._unknownStockFallback.load(
					std::memory_order_seq_cst);

			if (sequence
				== a_counters._sequence.load(
					std::memory_order_seq_cst)) {
				return snapshot;
			}
		}
	}
}
