#include "Render/EnginePixelShaderLookup.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <thread>

namespace cs::engine
{
	namespace
	{
		using ScopeState = EnginePixelShaderLookupScope::State;

		thread_local ScopeState g_current;
		std::atomic<std::uint64_t> g_callSequence{ 0 };

		struct CounterDelta
		{
			std::uint64_t returnsSeen = 0;
			std::uint64_t returnsScoped = 0;
			std::uint64_t returnsCaptured = 0;
			std::uint64_t returnsConsumed = 0;
			std::uint64_t discardedOutOfScope = 0;
			std::uint64_t discardedSubclassMismatch = 0;
			std::uint64_t discardedTechniqueMismatch = 0;
			std::uint64_t discardedDuplicate = 0;
			std::uint64_t discardedWithoutCreate = 0;
		};

		struct AtomicCounters
		{
			std::atomic_flag writer = ATOMIC_FLAG_INIT;
			std::atomic<std::uint64_t> version{ 0 };
			std::atomic<std::uint64_t> returnsSeen{ 0 };
			std::atomic<std::uint64_t> returnsScoped{ 0 };
			std::atomic<std::uint64_t> returnsCaptured{ 0 };
			std::atomic<std::uint64_t> returnsConsumed{ 0 };
			std::atomic<std::uint64_t> discardedOutOfScope{ 0 };
			std::atomic<std::uint64_t> discardedSubclassMismatch{ 0 };
			std::atomic<std::uint64_t> discardedTechniqueMismatch{ 0 };
			std::atomic<std::uint64_t> discardedDuplicate{ 0 };
			std::atomic<std::uint64_t> discardedWithoutCreate{ 0 };

			void Publish(const CounterDelta& a_delta) noexcept
			{
				while (writer.test_and_set(std::memory_order_acquire))
					std::this_thread::yield();
				version.fetch_add(1, std::memory_order_acq_rel);
				returnsSeen.fetch_add(
					a_delta.returnsSeen, std::memory_order_relaxed);
				returnsScoped.fetch_add(
					a_delta.returnsScoped, std::memory_order_relaxed);
				returnsCaptured.fetch_add(
					a_delta.returnsCaptured, std::memory_order_relaxed);
				returnsConsumed.fetch_add(
					a_delta.returnsConsumed, std::memory_order_relaxed);
				discardedOutOfScope.fetch_add(
					a_delta.discardedOutOfScope,
					std::memory_order_relaxed);
				discardedSubclassMismatch.fetch_add(
					a_delta.discardedSubclassMismatch,
					std::memory_order_relaxed);
				discardedTechniqueMismatch.fetch_add(
					a_delta.discardedTechniqueMismatch,
					std::memory_order_relaxed);
				discardedDuplicate.fetch_add(
					a_delta.discardedDuplicate,
					std::memory_order_relaxed);
				discardedWithoutCreate.fetch_add(
					a_delta.discardedWithoutCreate,
					std::memory_order_relaxed);
				version.fetch_add(1, std::memory_order_release);
				writer.clear(std::memory_order_release);
			}
		};

		AtomicCounters g_counters;
	}

	bool EnginePixelShaderLookupRelationshipsHold(
		const EnginePixelShaderLookupTelemetrySnapshot& a_snapshot) noexcept
	{
		return a_snapshot.returnsConsumed <= a_snapshot.returnsCaptured
			&& a_snapshot.returnsCaptured <= a_snapshot.returnsScoped
			&& a_snapshot.returnsScoped <= a_snapshot.returnsSeen;
	}

	std::string_view EnginePixelShaderLookupTargetName(
		EnginePixelShaderLookupTarget a_target) noexcept
	{
		switch (a_target) {
		case EnginePixelShaderLookupTarget::kBsdfLight:
			return "bsdf-light-pixel-shader-id";
		}
		return "unknown";
	}

	std::string_view EnginePixelShaderLookupTargetSubclass(
		EnginePixelShaderLookupTarget a_target) noexcept
	{
		switch (a_target) {
		case EnginePixelShaderLookupTarget::kBsdfLight:
			return "BSDFLightShader";
		}
		return {};
	}

	std::string_view EnginePixelShaderLookupTargetSymbol(
		EnginePixelShaderLookupTarget a_target) noexcept
	{
		switch (a_target) {
		case EnginePixelShaderLookupTarget::kBsdfLight:
			return "BSDFLightShaderMacros::GetPixelShaderID";
		}
		return {};
	}

	std::string_view EnginePixelShaderLookupCorrelationStatusName(
		EnginePixelShaderLookupCorrelationStatus a_status) noexcept
	{
		switch (a_status) {
		case EnginePixelShaderLookupCorrelationStatus::kMatched:
			return "matched";
		case EnginePixelShaderLookupCorrelationStatus::kUnavailable:
			return "unavailable";
		case EnginePixelShaderLookupCorrelationStatus::kAmbiguous:
			return "ambiguous";
		case EnginePixelShaderLookupCorrelationStatus::kRejected:
			return "rejected";
		}
		return "rejected";
	}

	std::string_view EnginePixelShaderLookupCorrelationReasonName(
		EnginePixelShaderLookupCorrelationReason a_reason) noexcept
	{
		switch (a_reason) {
		case EnginePixelShaderLookupCorrelationReason::kNone:
			return {};
		case EnginePixelShaderLookupCorrelationReason::kNoLookupObservation:
			return "no_lookup_observation";
		case EnginePixelShaderLookupCorrelationReason::
			kProductionLookupHookUnavailable:
			return "production_lookup_hook_unavailable";
		case EnginePixelShaderLookupCorrelationReason::kNoValidatedTarget:
			return "no_validated_target";
		case EnginePixelShaderLookupCorrelationReason::kMultipleMatchingReturns:
			return "multiple_matching_returns";
		case EnginePixelShaderLookupCorrelationReason::kOutOfScope:
			return "out_of_scope";
		case EnginePixelShaderLookupCorrelationReason::kShaderMismatch:
			return "shader_mismatch";
		case EnginePixelShaderLookupCorrelationReason::kSubclassMismatch:
			return "subclass_mismatch";
		case EnginePixelShaderLookupCorrelationReason::kRawTechniqueMismatch:
			return "raw_technique_mismatch";
		}
		return "out_of_scope";
	}

	EnginePixelShaderLookupScope::EnginePixelShaderLookupScope(
		void* a_shader,
		std::string_view a_subclass,
		std::uint32_t a_rawTechnique) noexcept :
		_previous(g_current)
	{
		g_current = {
			.shader = a_shader,
			.subclass = a_subclass,
			.rawTechnique = a_rawTechnique,
			.owner = this,
			.active = a_shader != nullptr && !a_subclass.empty()
		};
	}

	EnginePixelShaderLookupScope::~EnginePixelShaderLookupScope() noexcept
	{
		if (g_current.owner != this)
			return;
		if (g_current.active
			&& g_current.observation
			&& !g_current.consumed) {
			g_counters.Publish({
				.discardedWithoutCreate = 1
			});
		}
		g_current = _previous;
	}

	EnginePixelShaderLookupCorrelationResult
		EnginePixelShaderLookupScope::Snapshot(
			void* a_shader,
			std::string_view a_subclass,
			std::uint32_t a_rawTechnique) const noexcept
	{
		if (!g_current.active || g_current.owner != this) {
			return {
				.status =
					EnginePixelShaderLookupCorrelationStatus::kRejected,
				.reason =
					EnginePixelShaderLookupCorrelationReason::kOutOfScope
			};
		}
		if (g_current.shader != a_shader) {
			return {
				.status =
					EnginePixelShaderLookupCorrelationStatus::kRejected,
				.reason =
					EnginePixelShaderLookupCorrelationReason::kShaderMismatch
			};
		}
		if (g_current.subclass != a_subclass) {
			return {
				.status =
					EnginePixelShaderLookupCorrelationStatus::kRejected,
				.reason =
					EnginePixelShaderLookupCorrelationReason::kSubclassMismatch
			};
		}
		if (g_current.rawTechnique != a_rawTechnique) {
			return {
				.status =
					EnginePixelShaderLookupCorrelationStatus::kRejected,
				.reason = EnginePixelShaderLookupCorrelationReason::
					kRawTechniqueMismatch
			};
		}
		if (a_subclass
			!= EnginePixelShaderLookupTargetSubclass(
				EnginePixelShaderLookupTarget::kBsdfLight)) {
			return {
				.status =
					EnginePixelShaderLookupCorrelationStatus::kUnavailable,
				.reason = EnginePixelShaderLookupCorrelationReason::
					kNoValidatedTarget
			};
		}
		if (g_current.ambiguous) {
			return {
				.status =
					EnginePixelShaderLookupCorrelationStatus::kAmbiguous,
				.reason = EnginePixelShaderLookupCorrelationReason::
					kMultipleMatchingReturns
			};
		}
		if (!g_current.observation) {
			bool productionTargetInstalled = false;
			for (const auto& descriptor :
				GetEnginePixelShaderLookupTargetDescriptors()) {
				if (descriptor.target
						== EnginePixelShaderLookupTarget::kBsdfLight
					&& descriptor.installed) {
					productionTargetInstalled = true;
					break;
				}
			}
			return {
				.status =
					EnginePixelShaderLookupCorrelationStatus::kUnavailable,
				.reason = productionTargetInstalled
					? EnginePixelShaderLookupCorrelationReason::
						kNoLookupObservation
					: EnginePixelShaderLookupCorrelationReason::
						kProductionLookupHookUnavailable
			};
		}
		return {
			.status = EnginePixelShaderLookupCorrelationStatus::kMatched,
			.reason = EnginePixelShaderLookupCorrelationReason::kNone,
			.observation = g_current.observation
		};
	}

	void RecordEnginePixelShaderLookupReturn(
		EnginePixelShaderLookupTarget a_target,
		std::uint32_t a_functionInput,
		std::uint32_t a_returnedPsid) noexcept
	{
		CounterDelta delta{
			.returnsSeen = 1
		};
		if (!g_current.active) {
			delta.discardedOutOfScope = 1;
			g_counters.Publish(delta);
			return;
		}
		delta.returnsScoped = 1;
		if (g_current.subclass
			!= EnginePixelShaderLookupTargetSubclass(a_target)) {
			delta.discardedSubclassMismatch = 1;
			g_counters.Publish(delta);
			return;
		}
		if (g_current.rawTechnique != a_functionInput) {
			delta.discardedTechniqueMismatch = 1;
			g_counters.Publish(delta);
			return;
		}
		if (g_current.observation || g_current.ambiguous) {
			g_current.observation.reset();
			g_current.ambiguous = true;
			delta.discardedDuplicate = 1;
			g_counters.Publish(delta);
			return;
		}
		g_current.observation = EnginePixelShaderLookupObservation{
			.target = a_target,
			.functionInput = a_functionInput,
			.returnedPsid = EngineLookupPsid{ a_returnedPsid },
			.callSequence =
				g_callSequence.fetch_add(
					1, std::memory_order_relaxed) + 1,
			.threadId = GetCurrentThreadId()
		};
		delta.returnsCaptured = 1;
		g_counters.Publish(delta);
	}

	std::optional<EnginePixelShaderLookupObservation>
		ConsumeEnginePixelShaderLookup(
			void* a_shader,
			std::string_view a_subclass,
			std::uint32_t a_rawTechnique) noexcept
	{
		if (!g_current.active
			|| g_current.ambiguous
			|| g_current.consumed
			|| !g_current.observation
			|| g_current.shader != a_shader
			|| g_current.subclass != a_subclass
			|| g_current.rawTechnique != a_rawTechnique) {
			return std::nullopt;
		}
		g_current.consumed = true;
		g_counters.Publish({
			.returnsConsumed = 1
		});
		return g_current.observation;
	}

	void InstallEnginePixelShaderLookupHooks() noexcept
	{}

	EnginePixelShaderLookupInstallStats
		GetEnginePixelShaderLookupInstallStats() noexcept
	{
		return {};
	}

	std::span<const EnginePixelShaderLookupTargetDescriptor>
		GetEnginePixelShaderLookupTargetDescriptors() noexcept
	{
		return {};
	}

	EnginePixelShaderLookupTelemetrySnapshot
		SnapshotEnginePixelShaderLookupTelemetry() noexcept
	{
		for (;;) {
			const auto before =
				g_counters.version.load(std::memory_order_acquire);
			if ((before & 1u) != 0)
				continue;
			const EnginePixelShaderLookupTelemetrySnapshot result{
				.returnsSeen =
					g_counters.returnsSeen.load(std::memory_order_relaxed),
				.returnsScoped =
					g_counters.returnsScoped.load(std::memory_order_relaxed),
				.returnsCaptured =
					g_counters.returnsCaptured.load(std::memory_order_relaxed),
				.returnsConsumed =
					g_counters.returnsConsumed.load(std::memory_order_relaxed),
				.discardedOutOfScope =
					g_counters.discardedOutOfScope.load(
						std::memory_order_relaxed),
				.discardedSubclassMismatch =
					g_counters.discardedSubclassMismatch.load(
						std::memory_order_relaxed),
				.discardedTechniqueMismatch =
					g_counters.discardedTechniqueMismatch.load(
						std::memory_order_relaxed),
				.discardedDuplicate =
					g_counters.discardedDuplicate.load(
						std::memory_order_relaxed),
				.discardedWithoutCreate =
					g_counters.discardedWithoutCreate.load(
						std::memory_order_relaxed)
			};
			const auto after =
				g_counters.version.load(std::memory_order_acquire);
			if (before == after)
				return result;
		}
	}

#ifdef FO4CS_ENGINE_LOOKUP_TESTING
	void ResetEnginePixelShaderLookupForTesting() noexcept
	{
		g_current = {};
		g_callSequence.store(0, std::memory_order_relaxed);
		while (g_counters.writer.test_and_set(std::memory_order_acquire))
			std::this_thread::yield();
		g_counters.version.fetch_add(1, std::memory_order_acq_rel);
		g_counters.returnsSeen.store(0, std::memory_order_relaxed);
		g_counters.returnsScoped.store(0, std::memory_order_relaxed);
		g_counters.returnsCaptured.store(0, std::memory_order_relaxed);
		g_counters.returnsConsumed.store(0, std::memory_order_relaxed);
		g_counters.discardedOutOfScope.store(0, std::memory_order_relaxed);
		g_counters.discardedSubclassMismatch.store(
			0, std::memory_order_relaxed);
		g_counters.discardedTechniqueMismatch.store(
			0, std::memory_order_relaxed);
		g_counters.discardedDuplicate.store(0, std::memory_order_relaxed);
		g_counters.discardedWithoutCreate.store(
			0, std::memory_order_relaxed);
		g_counters.version.fetch_add(1, std::memory_order_release);
		g_counters.writer.clear(std::memory_order_release);
	}
#endif
}
