#pragma once

#include <cstdint>

namespace cs::features::ssgi
{
	struct Float2
	{
		float x = 0.0f;
		float y = 0.0f;

		friend constexpr bool operator==(const Float2&, const Float2&) = default;
	};

	// Top-left UV from clip-space NDC.
	[[nodiscard]] constexpr Float2 NDCToUV(Float2 a_ndc) noexcept
	{
		return { a_ndc.x * 0.5f + 0.5f, 0.5f - a_ndc.y * 0.5f };
	}

	// BSDFPrePass writes (currNDC - prevNDC) * (-0.5, +0.5).
	[[nodiscard]] constexpr Float2 MotionFromNDC(Float2 a_currNDC, Float2 a_prevNDC) noexcept
	{
		return {
			(a_currNDC.x - a_prevNDC.x) * -0.5f,
			(a_currNDC.y - a_prevNDC.y) * 0.5f
		};
	}

	// The stored motion already carries the sign, so reprojection adds it.
	[[nodiscard]] constexpr Float2 PreviousUV(Float2 a_currentUV, Float2 a_motion) noexcept
	{
		return { a_currentUV.x + a_motion.x, a_currentUV.y + a_motion.y };
	}

	enum class HistoryResetReason : std::uint32_t
	{
		kFirstFrame = 0,
		kResourceCreate,
		kResize,
		kShaderVariantChanged,
		kLoadingScreenClosed,
		kGenerationFailed
	};

	[[nodiscard]] constexpr const char* HistoryResetReasonName(HistoryResetReason a_reason) noexcept
	{
		switch (a_reason) {
		case HistoryResetReason::kFirstFrame:
			return "first_frame";
		case HistoryResetReason::kResourceCreate:
			return "resource_create";
		case HistoryResetReason::kResize:
			return "resize";
		case HistoryResetReason::kShaderVariantChanged:
			return "shader_variant_changed";
		case HistoryResetReason::kLoadingScreenClosed:
			return "loading_screen_closed";
		case HistoryResetReason::kGenerationFailed:
			return "generation_failed";
		}
		return "unknown";
	}

	// Ping-pong bookkeeping for the temporal history pair. Render-thread owned, no D3D.
	class HistoryState
	{
	public:
		struct Frame
		{
			bool          useHistory = false;
			std::uint32_t readIndex = 0;
			std::uint32_t writeIndex = 1;
		};

		[[nodiscard]] bool Valid() const noexcept { return _valid; }
		[[nodiscard]] bool Published() const noexcept { return _published; }
		[[nodiscard]] std::uint32_t ReadIndex() const noexcept { return _readIndex; }
		[[nodiscard]] std::uint32_t WriteIndex() const noexcept { return 1u - _readIndex; }
		[[nodiscard]] std::uint32_t ResetCount() const noexcept { return _resetCount; }
		[[nodiscard]] HistoryResetReason LastResetReason() const noexcept { return _lastResetReason; }

		// Skipped frames keep history; reprojection rejects what no longer matches, as upstream.
		[[nodiscard]] Frame Prepare() const noexcept
		{
			return { _valid, _readIndex, WriteIndex() };
		}

		void Reset(HistoryResetReason a_reason) noexcept
		{
			_valid = false;
			_clearPending = true;
			_lastResetReason = a_reason;
			++_resetCount;
		}

		void Publish() noexcept
		{
			_readIndex = WriteIndex();
			_valid = true;
			_published = true;
			_clearPending = false;
		}

		// True once per reset; the caller owns the matching GPU clear.
		[[nodiscard]] bool ConsumeClearPending() noexcept
		{
			const bool pending = _clearPending;
			_clearPending = false;
			return pending;
		}

	private:
		std::uint32_t      _readIndex = 0;
		std::uint32_t      _resetCount = 0;
		HistoryResetReason _lastResetReason = HistoryResetReason::kFirstFrame;
		bool               _valid = false;
		bool               _published = false;
		bool               _clearPending = true;
	};
}
