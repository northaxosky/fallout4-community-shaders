#pragma once

#include "Render/DxbcPatchArtifact.h"
#include "SssDxbcPatchRuntime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cs::features::sss_dxbc_patch
{
	inline constexpr bool kSchemaV1RouteIdentityExact = false;

	struct TelemetrySnapshot
	{
		std::uint64_t candidateSeen = 0;
		std::uint64_t exactRouteAdmitted = 0;
		std::uint64_t hashMismatch = 0;
		std::uint64_t preimageMismatch = 0;
		std::uint64_t patchBuilt = 0;
		std::uint64_t checksumHashMismatch = 0;
		std::uint64_t createPsAccepted = 0;
		std::uint64_t createPsRejected = 0;
		std::uint64_t identityPublished = 0;
		std::uint64_t patchedDrawMatched = 0;
		std::uint64_t stockShaderFallback = 0;
		std::uint64_t stockShaderFallbackFailed = 0;
		std::uint64_t realMaskBinds = 0;
		std::uint64_t whiteFallbackBinds = 0;
		std::uint64_t invariantViolations = 0;
		std::uint64_t unknownStockFallback = 0;
		std::size_t coveragePass = 0;
		std::size_t coverageCandidates = 0;
		bool artifactLoaded = false;
		bool routeIdentityExact = false;
		bool runtimeExact = false;
		bool resolverActive = false;
		bool brokerHookInstalled = false;
		bool dispatchPublished = false;
		bool bindingReady = false;
		bool invariantBroken = false;
		bool admissionReady = false;
		bool stockFallback = true;
	};

	struct RuntimePatchRegistrationDecision
	{
		bool artifactLoaded = false;
		bool routeIdentityExact = false;
		bool runtimeExact = false;
		bool registerResolver = false;
		bool registerPatchedDispatch = false;
	};

	inline constexpr RuntimePatchRegistrationDecision
		EvaluateRuntimePatchRegistration(
			bool a_artifactLoaded,
			bool a_routeIdentityExact,
			bool a_runtimeExact) noexcept
	{
		const bool registerRuntime =
			a_artifactLoaded
			&& a_routeIdentityExact
			&& a_runtimeExact;
		return {
			.artifactLoaded = a_artifactLoaded,
			.routeIdentityExact = a_routeIdentityExact,
			.runtimeExact = a_runtimeExact,
			.registerResolver = registerRuntime,
			.registerPatchedDispatch = registerRuntime
		};
	}

	struct PatchAdmissionSnapshot
	{
		bool ready = false;
		bool stockFallback = true;
	};

	inline constexpr PatchAdmissionSnapshot EvaluatePatchAdmission(
		bool a_artifactLoaded,
		bool a_routeIdentityExact,
		bool a_runtimeExact,
		bool a_resolverActive,
		bool a_brokerHookInstalled,
		bool a_dispatchPublished,
		bool a_bindingReady,
		bool a_invariantBroken) noexcept
	{
		const bool ready =
			a_artifactLoaded
			&& a_routeIdentityExact
			&& a_runtimeExact
			&& a_resolverActive
			&& a_brokerHookInstalled
			&& a_dispatchPublished
			&& a_bindingReady
			&& !a_invariantBroken;
		return {
			.ready = ready,
			.stockFallback = !ready
		};
	}

	class BindingReadinessLatch
	{
	public:
		struct Snapshot
		{
			bool ready = false;
			bool broken = false;
		};

		bool Publish(bool a_ready) noexcept
		{
			auto current = _state.load(std::memory_order_acquire);
			while (true) {
				if ((current & kBroken) != 0)
					return false;
				const auto desired = a_ready
					? static_cast<std::uint8_t>(current | kReady)
					: static_cast<std::uint8_t>(current & ~kReady);
				if (_state.compare_exchange_weak(
						current,
						desired,
						std::memory_order_acq_rel,
						std::memory_order_acquire)) {
					return a_ready;
				}
			}
		}

		void Break() noexcept
		{
			auto current = _state.load(std::memory_order_acquire);
			while (true) {
				const auto desired = static_cast<std::uint8_t>(
					(current | kBroken) & ~kReady);
				if (_state.compare_exchange_weak(
						current,
						desired,
						std::memory_order_acq_rel,
						std::memory_order_acquire)) {
					return;
				}
			}
		}

		[[nodiscard]] bool IsReady() const noexcept
		{
			return GetSnapshot().ready;
		}

		[[nodiscard]] bool IsBroken() const noexcept
		{
			return GetSnapshot().broken;
		}

		[[nodiscard]] Snapshot GetSnapshot() const noexcept
		{
			const auto state =
				_state.load(std::memory_order_acquire);
			const bool broken = (state & kBroken) != 0;
			return {
				.ready = (state & kReady) != 0 && !broken,
				.broken = broken
			};
		}

	private:
		static constexpr std::uint8_t kReady = 1u << 0;
		static constexpr std::uint8_t kBroken = 1u << 1;
		std::atomic_uint8_t _state{ 0 };
	};

	struct CounterDelta
	{
		std::uint64_t candidateSeen = 0;
		std::uint64_t exactRouteAdmitted = 0;
		std::uint64_t hashMismatch = 0;
		std::uint64_t preimageMismatch = 0;
		std::uint64_t patchBuilt = 0;
		std::uint64_t checksumHashMismatch = 0;
		std::uint64_t createPsAccepted = 0;
		std::uint64_t createPsRejected = 0;
		std::uint64_t identityPublished = 0;
		std::uint64_t patchedDrawMatched = 0;
		std::uint64_t stockShaderFallback = 0;
		std::uint64_t stockShaderFallbackFailed = 0;
		std::uint64_t realMaskBinds = 0;
		std::uint64_t whiteFallbackBinds = 0;
		std::uint64_t invariantViolations = 0;
		std::uint64_t unknownStockFallback = 0;
	};

	class AtomicCounters
	{
	public:
		void Publish(const CounterDelta& a_delta) noexcept;

	private:
		friend TelemetrySnapshot SnapshotCounters(
			const AtomicCounters& a_counters) noexcept;

		std::atomic_uint64_t _sequence{ 0 };
		std::atomic_uint64_t _candidateSeen{ 0 };
		std::atomic_uint64_t _exactRouteAdmitted{ 0 };
		std::atomic_uint64_t _hashMismatch{ 0 };
		std::atomic_uint64_t _preimageMismatch{ 0 };
		std::atomic_uint64_t _patchBuilt{ 0 };
		std::atomic_uint64_t _checksumHashMismatch{ 0 };
		std::atomic_uint64_t _createPsAccepted{ 0 };
		std::atomic_uint64_t _createPsRejected{ 0 };
		std::atomic_uint64_t _identityPublished{ 0 };
		std::atomic_uint64_t _patchedDrawMatched{ 0 };
		std::atomic_uint64_t _stockShaderFallback{ 0 };
		std::atomic_uint64_t _stockShaderFallbackFailed{ 0 };
		std::atomic_uint64_t _realMaskBinds{ 0 };
		std::atomic_uint64_t _whiteFallbackBinds{ 0 };
		std::atomic_uint64_t _invariantViolations{ 0 };
		std::atomic_uint64_t _unknownStockFallback{ 0 };
	};
	static_assert(std::atomic_uint64_t::is_always_lock_free);

#ifdef FO4CS_SSS_DXBC_PATCH_TESTING
	enum class SnapshotTestPoint : std::uint8_t
	{
		kCandidateSeen,
		kPatchedDrawMatched
	};
	using SnapshotTestHook = void (*)(SnapshotTestPoint) noexcept;
	void SetSnapshotTestHook(SnapshotTestHook a_hook) noexcept;
#endif

	engine::PixelShaderSwapResolverResult ResolveRequest(
		const engine::dxbc_patch::Artifact& a_artifact,
		bool a_routeIdentityExact,
		bool a_runtimeExact,
		bool a_dispatchPublished,
		bool a_bindingReady,
		AtomicCounters& a_counters,
		const engine::PixelShaderSwapRequest& a_request,
		CreatePatchedPixelShaderFunction a_create) noexcept;
	TelemetrySnapshot SnapshotCounters(
		const AtomicCounters& a_counters) noexcept;
	inline constexpr bool TelemetryRelationshipsHold(
		const TelemetrySnapshot& a_snapshot) noexcept
	{
		const auto admission = EvaluatePatchAdmission(
			a_snapshot.artifactLoaded,
			a_snapshot.routeIdentityExact,
			a_snapshot.runtimeExact,
			a_snapshot.resolverActive,
			a_snapshot.brokerHookInstalled,
			a_snapshot.dispatchPublished,
			a_snapshot.bindingReady,
			a_snapshot.invariantBroken);
		return a_snapshot.exactRouteAdmitted <= a_snapshot.candidateSeen
			&& a_snapshot.patchBuilt <= a_snapshot.exactRouteAdmitted
			&& a_snapshot.createPsAccepted + a_snapshot.createPsRejected
				<= a_snapshot.patchBuilt
			&& a_snapshot.identityPublished
				<= a_snapshot.createPsAccepted
			&& !(a_snapshot.bindingReady
				&& a_snapshot.invariantBroken)
			&& a_snapshot.admissionReady == admission.ready
			&& a_snapshot.stockFallback == admission.stockFallback
			&& (!a_snapshot.artifactLoaded
				|| (a_snapshot.coveragePass <= a_snapshot.coverageCandidates
					&& a_snapshot.coverageCandidates != 0));
	}
}
