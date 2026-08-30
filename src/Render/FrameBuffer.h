#pragma once

#include "Render/FrameBufferMath.h"

#include <cstdint>

struct ID3D11DeviceContext;

namespace cs::engine
{
	enum class FrameBufferPublishSource : std::uint8_t
	{
		kNone,
		kFullscreenLightDraw
	};

	enum class FrameBufferRejectReason : std::uint8_t
	{
		kNone,
		kMissingSnapshot,
		kStaleSnapshot,
		kBufferTooSmall,
		kInvalidCameraBasis,
		kInvalidProjection,
		kInvalidOrigin,
		kNearZeroOrigin
	};

	// The engine's own b12 contents, mirrored byte-for-byte from its Unmap.
	struct FrameBufferSnapshot
	{
		FrameBuffer              data{};
		std::uint64_t            sequence = 0;
		std::uint32_t            frameCount = 0;
		FrameBufferPublishSource source = FrameBufferPublishSource::kNone;
		bool                     valid = false;
	};

	struct FrameBufferStatus
	{
		bool          hookInstalled = false;
		bool          hookedContextIsCurrent = false;
		bool          identified = false;
		const char*   identitySource = "none";
		bool          contextMatchesSlot12 = false;
		std::uint32_t byteWidth = 0;
		std::uint32_t usage = 0;
		std::uint32_t cpuAccessFlags = 0;
		std::uint32_t bindFlags = 0;
		std::uint64_t snapshots = 0;
		std::uint32_t mapsLastFrame = 0;
		std::uint32_t maxMapsPerFrame = 0;
		bool          latestSnapshotValid = false;
		bool          publishedSnapshotValid = false;
		bool          publishedThisFrame = false;
		std::uint32_t latestFrameCount = 0;
		std::uint32_t publishedFrameCount = 0;
		std::uint32_t fullscreenLightAnchorsThisFrame = 0;
		std::uint32_t publicationsThisFrame = 0;
		std::uint32_t rejectionsThisFrame = 0;
		std::uint32_t distinctCamerasThisFrame = 0;
		float         minimumOriginMagnitude = 0.0f;
		std::uint64_t latestSequence = 0;
		std::uint64_t publishedSequence = 0;
		std::uint64_t publishedCameraHash = 0;
		std::uint64_t completedFrames = 0;
		std::uint64_t publicationFrames = 0;
		std::uint64_t noPublicationFrames = 0;
		std::uint64_t nearZeroOriginRejections = 0;
		FrameBufferPublishSource publishSource = FrameBufferPublishSource::kNone;
		FrameBufferRejectReason  lastRejectReason = FrameBufferRejectReason::kNone;
		FrameBufferSnapshot fullscreenLight{};
	};

	// Startup thread only; registers the per-frame identity resolve.
	void InstallFrameBuffer();

	// Installs the Map/Unmap detours once the immediate context exists.
	void OnFrameBufferD3D11Ready(ID3D11DeviceContext* a_context);

	// Render thread only. Returns the world-camera snapshot published at an exact draw.
	[[nodiscard]] const FrameBufferSnapshot& GetFrameBuffer() noexcept;

	// Render thread only. Raw latest Unmap snapshot for diagnostics.
	[[nodiscard]] const FrameBufferSnapshot& GetLatestFrameBuffer() noexcept;

	[[nodiscard]] FrameBufferStatus GetFrameBufferStatus() noexcept;
	[[nodiscard]] const char* FrameBufferPublishSourceName(
		FrameBufferPublishSource a_source) noexcept;
	[[nodiscard]] const char* FrameBufferRejectReasonName(
		FrameBufferRejectReason a_reason) noexcept;
}
