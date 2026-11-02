#pragma once

#include "Render/PrecipitationOcclusion.h"

#include <array>
#include <atomic>
#include <cstdint>

struct ID3D11DepthStencilView;

namespace RE
{
	class BSGeometry;
	class BSRenderPass;
	class BSLightingShaderProperty;
}

namespace cs::engine
{
	// Opaque static occluders drawn in a few batched calls instead of one engine draw each.
	enum class BatchClass : std::uint8_t
	{
		kPlain,
		kMergeInstanced,
		kCount
	};

	// Why an accepted occluder stays on the engine path.
	enum class BatchFallback : std::uint8_t
	{
		// Alpha test, tree animation, additional alpha mask or another technique we do not draw.
		kTechnique,
		// Shader property or alpha property controllers: the engine builds no draw for them.
		kAnimated,
		kRendererData,
		kBufferNotReady,
		kStructuredNotReady,
		kHeapView,
		kPoolFull,
		kArenaFull,
		kCount
	};

	// Slot 44 runs on worker threads, so relaxed atomics.
	struct BatchStats
	{
		std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(BatchClass::kCount)> batched{};
		std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(BatchFallback::kCount)> fallback{};
		std::atomic<std::uint64_t> draws{ 0 };
		std::atomic<std::uint64_t> validations{ 0 };
		std::atomic<std::uint64_t> validationFailures{ 0 };
		// Last validation: texels that differ and the largest D16 step difference.
		std::atomic<std::uint32_t> mismatchedTexels{ 0 };
		std::atomic<std::uint32_t> maxDelta{ 0 };
		std::atomic<std::uint32_t> poolRecords{ 0 };
		// Validation only: each record against the engine pass built for the same shape.
		std::atomic<std::uint32_t> reconciled{ 0 };
		// The engine's command buffer baked a different world matrix than the shape has now.
		std::atomic<std::uint32_t> staleWorld{ 0 };
		// The engine's baked index count differs from ours.
		std::atomic<std::uint32_t> countDiffers{ 0 };
		// The engine pass was built at a mesh LOD mode other than the default.
		std::atomic<std::uint32_t> lodNotDefault{ 0 };
		// The engine built no pass for a shape we recorded.
		std::atomic<std::uint32_t> noEnginePass{ 0 };
		// Water passes the engine took in the running capture; the batch never covers them.
		std::atomic<std::uint32_t> waterPasses{ 0 };
	};

	[[nodiscard]] BatchStats& GetBatchStats() noexcept;

	enum class BatchDecision : std::uint8_t
	{
		// Build the engine pass as usual.
		kStock,
		// Recorded for the batch; the thunk returns no pass.
		kRecorded
	};

	// Where a recorded occluder lives in its arena, for validation to reconcile.
	struct BatchSlot
	{
		float* world = nullptr;
		std::uint32_t* indexCount = nullptr;
	};

	// Slot-44 thunk, any thread. a_technique is the stock occlusion technique.
	// a_slot is set when the occluder was recorded.
	[[nodiscard]] BatchDecision RecordOccluder(
		OccluderBatchMode a_mode,
		const RE::BSLightingShaderProperty& a_property,
		RE::BSGeometry& a_geometry,
		std::uint32_t a_technique,
		BatchSlot& a_slot);

	// Validation, any thread: counts how a record differs from the engine pass built for
	// the same shape: baked world, index count, LOD mode, or no pass at all.
	void ReconcileWithEnginePass(const BatchSlot& a_slot, const RE::BSRenderPass* a_pass);

	// Validation, render thread: the captures between these batch only the classes in
	// a_mask (a bit per BatchClass) and leave the telemetry counters alone.
	void BeginValidationCapture(std::uint32_t a_mask) noexcept;
	void EndValidationCapture() noexcept;
	void ResetReconcileStats() noexcept;

	// Render thread, before a capture: false when this runtime or device cannot batch.
	// Otherwise it clears the recorded set and readies the draw resources.
	[[nodiscard]] bool PrepareOccluderBatch();

	// Render thread, after the engine's pass: draws the recorded set with the engine's
	// per-frame constants, which still hold the occlusion camera.
	void DrawOccluderBatch(ID3D11DepthStencilView* a_target, std::uint32_t a_extent, bool a_clear);

	// Records of the last capture by class and cull group, for validation logs.
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
