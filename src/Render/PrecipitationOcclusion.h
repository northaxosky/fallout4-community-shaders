#pragma once

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
	};

	// Null unless a capture runs and the accumulator is its occlusion one.
	[[nodiscard]] const ActiveCapture* GetActiveCapture(const RE::BSShaderAccumulator* a_accumulator) noexcept;

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

	using CaptureClock = std::chrono::steady_clock;

	// Capture CPU time; the hook parts run inside it, possibly on worker threads.
	struct CaptureTimings
	{
		std::atomic<CaptureClock::rep> capture{ 0 };
		std::atomic<CaptureClock::rep> hookPredicate{ 0 };
		std::atomic<CaptureClock::rep> hookStock{ 0 };
		std::atomic<CaptureClock::rep> hookOwn{ 0 };
	};

	[[nodiscard]] CaptureTimings& GetCaptureTimings() noexcept;

	[[nodiscard]] inline double TicksToMs(CaptureClock::rep a_ticks) noexcept
	{
		return std::chrono::duration<double, std::milli>(CaptureClock::duration(a_ticks)).count();
	}
}
