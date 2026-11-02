#pragma once

#include "Render/PrecipitationOcclusion.h"

#include <array>
#include <atomic>
#include <cstdint>

struct ID3D11DepthStencilView;

namespace RE
{
	class BSGeometry;
	class BSLightingShaderProperty;
}

namespace cs::engine
{
	// Opaque static occluders are drawn in a few batched calls.
	enum class BatchClass : std::uint8_t
	{
		kPlain,
		kMergeInstanced,
		kCount
	};

	// Why an accepted occluder stays on the engine path.
	enum class BatchFallback : std::uint8_t
	{
		// Alpha test, tree animation, alpha mask or another technique we skip.
		kTechnique,
		// Animated properties get no engine draw either.
		kAnimated,
		kRendererData,
		kBufferNotReady,
		kStructuredNotReady,
		kHeapView,
		kPoolFull,
		kArenaFull,
		kCount
	};

	struct BatchStats
	{
		std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(BatchClass::kCount)> batched{};
		std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(BatchFallback::kCount)> fallback{};
		std::atomic<std::uint64_t> draws{ 0 };
		std::atomic<std::uint64_t> validations{ 0 };
		std::atomic<std::uint64_t> validationFailures{ 0 };
		// Last validation: differing texels and the largest D16 step difference.
		std::atomic<std::uint32_t> mismatchedTexels{ 0 };
		std::atomic<std::uint32_t> maxDelta{ 0 };
		std::atomic<std::uint32_t> poolRecords{ 0 };
	};

	[[nodiscard]] BatchStats& GetBatchStats() noexcept;

	enum class BatchDecision : std::uint8_t
	{
		// Build the engine pass as usual.
		kStock,
		// Recorded for the batch; the thunk returns no pass.
		kRecorded
	};

	// Slot-44 thunk, any thread. a_technique is the stock occlusion technique.
	[[nodiscard]] BatchDecision RecordOccluder(
		OccluderBatchMode a_mode,
		const RE::BSLightingShaderProperty& a_property,
		RE::BSGeometry& a_geometry,
		std::uint32_t a_technique);

	// Validation captures batch only the classes in a_mask, a bit per BatchClass.
	void BeginValidationCapture(std::uint32_t a_mask) noexcept;
	void EndValidationCapture() noexcept;

	// Render thread, before a capture: false if this runtime cannot batch.
	[[nodiscard]] bool PrepareOccluderBatch();

	// Render thread, after the engine's pass, while the camera is still bound.
	void DrawOccluderBatch(ID3D11DepthStencilView* a_target, std::uint32_t a_extent);

	// Records of the last capture by class, for validation logs.
	struct BatchRecorded
	{
		std::uint32_t plain = 0;
		std::uint32_t mergeInstanced = 0;
	};
	[[nodiscard]] BatchRecorded GetBatchRecorded() noexcept;

	// Two scratch D16 maps for validation: the engine's draw and the batched draw.
	struct BatchValidationTargets
	{
		OcclusionCaptureTarget engine;
		OcclusionCaptureTarget batch;
	};

	[[nodiscard]] bool AcquireValidationTargets(BatchValidationTargets& a_targets);

	struct BatchValidationResult
	{
		bool ok = false;
		std::uint32_t mismatchedTexels = 0;
		std::uint32_t maxDelta = 0;
		// Texels where the batch is nearer than the engine, and the reverse.
		std::uint32_t batchNearer = 0;
		std::uint32_t engineNearer = 0;
		// Texel bounds of the mismatch: x0, y0, x1, y1.
		std::array<std::uint32_t, 4> bounds{};
	};

	// Blocks on the GPU; development only.
	[[nodiscard]] BatchValidationResult CompareValidationTargets(std::uint32_t a_extent);
}
