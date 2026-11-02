#pragma once

#include "Render/EngineCallSite.h"
#include "Telemetry/Telemetry.h"

#include <DirectXMath.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace RE
{
	class BSShaderAccumulator;
	class NiCamera;
}

namespace cs::engine
{
	// Upstream occluder filter inputs; they travel with the capture.
	struct OccluderPolicy
	{
		float minOccluderRadius = 0.0f;
		float belowGridMargin = 0.0f;
		// World height of the probe grid's bottom layer; lowest float disables the cut.
		float probeGridBottomZ = -3.402823466e+38f;
	};

	// How the slot-44 thunk routes batchable occluders during a capture.
	enum class OccluderBatchMode : std::uint8_t
	{
		kOff,
		// Record batchable occluders and skip the engine's pass; draw them afterward.
		kBatch,
		// Record batchable occluders to reconcile them with the engine's passes; the engine
		// still draws everything and the batch draws nothing.
		kValidate
	};

	struct OccluderBatchOptions
	{
		bool enabled = false;
		// Every Nth capture also diffs the engine's depth against the batch's; 0 is off.
		std::uint32_t validationInterval = 0;
	};

	// Replaces the engine's precipitation occlusion depth target for one capture.
	struct OcclusionCaptureTarget
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11DepthStencilView* depthView = nullptr;
		ID3D11ShaderResourceView* depthSRV = nullptr;
	};

	struct OcclusionCaptureRequest
	{
		float boxSize = 0.0f;
		// Precipitation travel direction written to the engine's direction global.
		DirectX::XMFLOAT3 travelDirection{};
		// Ortho frustum quarter of the box: 0 to 3.
		std::uint32_t quadrant = 0;
		OccluderPolicy occluders;
		OccluderBatchOptions batching;
		OcclusionCaptureTarget target;
	};

	struct OcclusionCaptureResult
	{
		// Engine matrix as stored: rows map (world - main camera) to occlusion NDC.
		DirectX::XMFLOAT4X4 matrix{};
		// DS8's platform entry resolves to the same slot and holds the stock target.
		bool stockTargetRestored = false;
	};

	enum class OcclusionCaptureStatus : std::uint8_t
	{
		kCaptured,
		kNoPrecipitation,
		kTargetsUnavailable
	};

	struct HookInstall
	{
		std::string name;
		bool installed = false;
		std::string detail;
	};

	// Registers the capture thunks and the occluder pass hook; call during Load.
	[[nodiscard]] std::vector<HookInstall> InstallOcclusionCaptureHooks();

	// Redirects a verified call site through a_install (stl::write_thunk_call).
	[[nodiscard]] HookInstall InstallCallSiteHook(const CallSiteAnchor& a_anchor, void (*a_install)(std::uintptr_t));

	// Runs one capture after the stock pass; restores every engine override.
	[[nodiscard]] OcclusionCaptureStatus CaptureOcclusion(
		const OcclusionCaptureRequest& a_request,
		OcclusionCaptureResult& a_result);

	// Resolved stock targets for the startup log; empty parts are unallocated.
	[[nodiscard]] std::string DescribeStockOcclusionTargets();

	// Capture state shared with the hooks that fire inside the engine's pass.
	struct ActiveCapture
	{
		std::atomic<const RE::BSShaderAccumulator*> accumulator{ nullptr };
		std::uint32_t quadrant = 0;
		OccluderPolicy occluders;
		const RE::NiCamera* camera = nullptr;
		OccluderBatchMode batchMode = OccluderBatchMode::kOff;
		// The thread that runs the capture's scene walk.
		std::uint32_t walkThread = 0;
	};

	// Null unless a capture runs and the accumulator is its occlusion one.
	[[nodiscard]] const ActiveCapture* GetActiveCapture(const RE::BSShaderAccumulator* a_accumulator) noexcept;

	// Null unless a capture runs and the caller is on its walk thread.
	[[nodiscard]] const ActiveCapture* GetActiveCaptureOnWalkThread() noexcept;

	// Telemetry counters; slot 44 runs on worker threads, so relaxed atomics.
	enum class OccluderReject : std::uint8_t
	{
		kSkinned,
		kFlags,
		kRadius,
		kBelowGrid,
		kBsx,
		kCount
	};

	// Why the stock builder rejects an occluder that upstream accepts.
	enum class OwnBuildReason : std::uint8_t
	{
		kLandscape,
		kNotCasting,
		kAlphaBlended,
		kOther,
		kCount
	};

	struct OccluderStats
	{
		std::atomic<std::uint64_t> accepted{ 0 };
		std::atomic<std::uint64_t> delegated{ 0 };
		// Own passes handed to the engine, new or reused, by reason.
		std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(OwnBuildReason::kCount)> ownBuilt{};
		// Own passes whose command buffer was created, not reused.
		std::atomic<std::uint64_t> ownNew{ 0 };
		// Upstream-accepted occluders stock rejected and the own build could not take.
		std::atomic<std::uint64_t> stockOnly{ 0 };
		std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(OccluderReject::kCount)> rejected{};
	};

	[[nodiscard]] OccluderStats& GetOccluderStats() noexcept;

	// Culling-group seeding counters; written once per scene root, relaxed atomics.
	struct GatherStats
	{
		std::atomic<std::uint64_t> rootsTested{ 0 };
		std::atomic<std::uint64_t> rootsCulled{ 0 };
		std::atomic<std::uint64_t> leavesFed{ 0 };
		// Child subtrees and leaves skipped as outside the capture frustum.
		std::atomic<std::uint64_t> nodesPruned{ 0 };
	};

	[[nodiscard]] GatherStats& GetGatherStats() noexcept;

	using CaptureClock = std::chrono::steady_clock;

	// Capture CPU time; passes sums every worker thread's slot-44 time.
	struct CaptureTimings
	{
		std::atomic<CaptureClock::rep> capture{ 0 };
		std::atomic<CaptureClock::rep> gather{ 0 };
		std::atomic<CaptureClock::rep> passes{ 0 };
		// The batched occluder draw, on the render thread.
		std::atomic<CaptureClock::rep> batch{ 0 };
	};

	[[nodiscard]] CaptureTimings& GetCaptureTimings() noexcept;

	// Adds the scope's duration to a slot; reads no clock unless telemetry runs.
	class ScopedCaptureTimer
	{
	public:
		explicit ScopedCaptureTimer(std::atomic<CaptureClock::rep>& a_slot) noexcept :
			_slot(a_slot), _timed(telemetry::pump::Enabled())
		{
			if (_timed)
				_begin = CaptureClock::now();
		}

		~ScopedCaptureTimer() noexcept
		{
			if (_timed)
				_slot.fetch_add((CaptureClock::now() - _begin).count(), std::memory_order_relaxed);
		}

		ScopedCaptureTimer(const ScopedCaptureTimer&) = delete;
		ScopedCaptureTimer& operator=(const ScopedCaptureTimer&) = delete;

	private:
		std::atomic<CaptureClock::rep>& _slot;
		CaptureClock::time_point _begin{};
		bool _timed;
	};

	[[nodiscard]] inline double TicksToMs(CaptureClock::rep a_ticks) noexcept
	{
		return std::chrono::duration<double, std::milli>(CaptureClock::duration(a_ticks)).count();
	}
}
