#include "Render/FrameBuffer.h"

#include "Log.h"
#include "LogThrottle.h"
#include "PCH.h"
#include "Render/Engine.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <limits>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.framebuffer");

		constexpr UINT kPerFrameSlot = 12;
		constexpr UINT kMinimumByteWidth = static_cast<UINT>(sizeof(FrameBuffer));

		using MapFunction = HRESULT(STDMETHODCALLTYPE*)(
			ID3D11DeviceContext*,
			ID3D11Resource*,
			UINT,
			D3D11_MAP,
			UINT,
			D3D11_MAPPED_SUBRESOURCE*);
		using UnmapFunction = void(STDMETHODCALLTYPE*)(
			ID3D11DeviceContext*,
			ID3D11Resource*,
			UINT);

		struct AnchorFrameState
		{
			std::uint32_t frameCount = std::numeric_limits<std::uint32_t>::max();
			FrameBufferSnapshot lastCamera{};
			FrameBufferSnapshot fullscreenLight{};
		};

		bool g_installed = false;
		std::atomic_bool g_hookInstalled{ false };
		std::atomic<ID3D11DeviceContext*> g_hookedContext{ nullptr };
		std::atomic_bool g_hookedContextIsCurrent{ false };

		std::atomic<ID3D11Resource*> g_identity{ nullptr };
		std::atomic<const char*> g_identitySource{ "none" };
		std::atomic_bool g_contextMatchesSlot12{ false };
		std::atomic<UINT> g_byteWidth{ 0 };
		std::atomic<UINT> g_usage{ 0 };
		std::atomic<UINT> g_cpuAccessFlags{ 0 };
		std::atomic<UINT> g_bindFlags{ 0 };

		std::atomic<std::uint64_t> g_snapshots{ 0 };
		std::atomic<std::uint32_t> g_mapsThisFrame{ 0 };
		std::atomic<std::uint32_t> g_mapsLastFrame{ 0 };
		std::atomic<std::uint32_t> g_maxMapsPerFrame{ 0 };

		std::atomic<std::uint32_t> g_fullscreenLightAnchorsThisFrame{ 0 };
		std::atomic<std::uint32_t> g_publicationsThisFrame{ 0 };
		std::atomic<std::uint32_t> g_rejectionsThisFrame{ 0 };
		std::atomic<std::uint32_t> g_distinctCamerasThisFrame{ 0 };
		std::atomic<std::uint64_t> g_completedFrames{ 0 };
		std::atomic<std::uint64_t> g_publicationFrames{ 0 };
		std::atomic<std::uint64_t> g_noPublicationFrames{ 0 };
		std::atomic<std::uint64_t> g_nearZeroOriginRejections{ 0 };
		std::atomic<FrameBufferRejectReason> g_lastRejectReason{
			FrameBufferRejectReason::kNone
		};

		// Written and consumed on the render thread.
		FrameBufferSnapshot g_latestSnapshot{};
		FrameBufferSnapshot g_worldSnapshot{};
		AnchorFrameState g_anchorFrame{};
		const void* g_mapped = nullptr;

		[[nodiscard]] std::uint32_t CurrentEngineFrame() noexcept
		{
			auto* state = GetGraphicsState();
			return state ? state->frameCount : 0u;
		}

		[[nodiscard]] ID3D11Buffer* BoundAtPixelSlot12(
			ID3D11DeviceContext* a_context) noexcept
		{
			ID3D11Buffer* bound = nullptr;
			a_context->PSGetConstantBuffers(kPerFrameSlot, 1, &bound);
			return bound;
		}

		[[nodiscard]] ID3D11Buffer* EnginePerFrameBuffer() noexcept
		{
			// A null base would otherwise become a plausible member-offset pointer.
			auto* context = GetActiveContext();
			return context ?
				reinterpret_cast<ID3D11Buffer*>(context->perFrameConstantBuffer) :
				nullptr;
		}

		void RecordDescription(const D3D11_BUFFER_DESC& a_desc) noexcept
		{
			g_byteWidth.store(a_desc.ByteWidth, std::memory_order_relaxed);
			g_usage.store(static_cast<UINT>(a_desc.Usage), std::memory_order_relaxed);
			g_cpuAccessFlags.store(a_desc.CPUAccessFlags, std::memory_order_relaxed);
			g_bindFlags.store(a_desc.BindFlags, std::memory_order_relaxed);
		}

		bool AdoptIdentity(ID3D11Buffer* a_buffer, const char* a_source)
		{
			if (!a_buffer) {
				return false;
			}

			D3D11_BUFFER_DESC desc{};
			a_buffer->GetDesc(&desc);
			RecordDescription(desc);
			if ((desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0
				|| desc.ByteWidth < kMinimumByteWidth) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Rejected the per-frame constant buffer candidate from {}: "
					"bind flags {:#x}, byte width {}.",
					a_source,
					desc.BindFlags,
					desc.ByteWidth);
				return false;
			}

			const auto previous = g_identity.exchange(a_buffer, std::memory_order_relaxed);
			const auto previousSource =
				g_identitySource.exchange(a_source, std::memory_order_relaxed);
			if (previous != a_buffer || previousSource != a_source) {
				L->info(
					"Per-frame constant buffer identified via {} at {:#x} "
					"(byte width {}, usage {}, cpu access {:#x}).",
					a_source,
					reinterpret_cast<std::uintptr_t>(a_buffer),
					desc.ByteWidth,
					static_cast<UINT>(desc.Usage),
					desc.CPUAccessFlags);
			}
			return true;
		}

		// Re-resolved every frame so a device reset or resolution change heals itself.
		void ResolveIdentity()
		{
			auto* context = GetImmediateContext();
			if (!context) {
				return;
			}
			g_hookedContextIsCurrent.store(
				context == g_hookedContext.load(std::memory_order_relaxed),
				std::memory_order_relaxed);

			auto* bound = BoundAtPixelSlot12(context);
			auto* fromEngine = EnginePerFrameBuffer();
			g_contextMatchesSlot12.store(
				bound != nullptr && bound == fromEngine,
				std::memory_order_relaxed);

			if (bound) {
				AdoptIdentity(bound, "ps_slot12");
				bound->Release();
			} else {
				AdoptIdentity(fromEngine, "context");
			}
		}

		void UpdateMapCounters(std::uint32_t a_frame) noexcept
		{
			if (a_frame != g_latestSnapshot.frameCount) {
				g_mapsLastFrame.store(
					g_mapsThisFrame.load(std::memory_order_relaxed),
					std::memory_order_relaxed);
				g_mapsThisFrame.store(1, std::memory_order_relaxed);
			} else {
				g_mapsThisFrame.fetch_add(1, std::memory_order_relaxed);
			}

			const auto count = g_mapsThisFrame.load(std::memory_order_relaxed);
			auto maximum = g_maxMapsPerFrame.load(std::memory_order_relaxed);
			while (maximum < count
				&& !g_maxMapsPerFrame.compare_exchange_weak(
					maximum,
					count,
					std::memory_order_relaxed)) {
			}
		}

		void CaptureSnapshot() noexcept
		{
			const auto* source = g_mapped;
			g_mapped = nullptr;

			const auto width = g_byteWidth.load(std::memory_order_relaxed);
			const auto bytes = std::min<std::size_t>(width, sizeof(FrameBuffer));
			if (!source || bytes == 0) {
				return;
			}

			std::memset(&g_latestSnapshot.data, 0, sizeof(g_latestSnapshot.data));
			std::memcpy(&g_latestSnapshot.data, source, bytes);

			const auto frame = CurrentEngineFrame();
			UpdateMapCounters(frame);
			g_latestSnapshot.frameCount = frame;
			g_latestSnapshot.sequence =
				g_snapshots.fetch_add(1, std::memory_order_relaxed) + 1;
			g_latestSnapshot.source = FrameBufferPublishSource::kNone;
			g_latestSnapshot.valid = width >= kMinimumByteWidth;
		}

		struct FrameBufferMap_Hook
		{
			static HRESULT STDMETHODCALLTYPE thunk(
				ID3D11DeviceContext* a_this,
				ID3D11Resource* a_resource,
				UINT a_subresource,
				D3D11_MAP a_mapType,
				UINT a_mapFlags,
				D3D11_MAPPED_SUBRESOURCE* a_mapped)
			{
				if (!func) {
					return E_POINTER;
				}
				const HRESULT result =
					func(a_this, a_resource, a_subresource, a_mapType, a_mapFlags, a_mapped);
				if (a_resource == g_identity.load(std::memory_order_relaxed)
					&& a_subresource == 0) {
					g_mapped =
						result == S_OK && a_mapped ? a_mapped->pData : nullptr;
				}
				return result;
			}

			static inline MapFunction func = nullptr;
		};

		struct FrameBufferUnmap_Hook
		{
			static void STDMETHODCALLTYPE thunk(
				ID3D11DeviceContext* a_this,
				ID3D11Resource* a_resource,
				UINT a_subresource)
			{
				// The mapped pointer dies at Unmap, so snapshot before handing off.
				if (a_resource == g_identity.load(std::memory_order_relaxed)
					&& a_subresource == 0
					&& g_mapped) {
					CaptureSnapshot();
				}
				if (func) {
					func(a_this, a_resource, a_subresource);
				}
			}

			static inline UnmapFunction func = nullptr;
		};

		[[nodiscard]] std::uint64_t HashCamera(const FrameBuffer& a_frameBuffer) noexcept
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			const auto add = [&](float a_value) {
				hash ^= std::bit_cast<std::uint32_t>(a_value);
				hash *= prime;
			};

			for (const auto& row : a_frameBuffer.ViewToWorld) {
				add(row.x);
				add(row.y);
				add(row.z);
				add(row.w);
			}
			const auto origin = CameraWorldOrigin(a_frameBuffer);
			add(origin.x);
			add(origin.y);
			add(origin.z);
			return hash;
		}

		[[nodiscard]] bool CameraMateriallyDiffers(
			const FrameBuffer& a_lhs,
			const FrameBuffer& a_rhs) noexcept
		{
			constexpr float rowTolerance = 1e-4f;
			constexpr float originTolerance = 1e-2f;
			float maxRowDelta = 0.0f;
			for (std::size_t row = 0; row < 3; ++row) {
				const auto& lhs = a_lhs.ViewToWorld[row];
				const auto& rhs = a_rhs.ViewToWorld[row];
				maxRowDelta = std::max({
					maxRowDelta,
					std::abs(lhs.x - rhs.x),
					std::abs(lhs.y - rhs.y),
					std::abs(lhs.z - rhs.z)
				});
			}
			const auto lhsOrigin = CameraWorldOrigin(a_lhs);
			const auto rhsOrigin = CameraWorldOrigin(a_rhs);
			const float deltaX = lhsOrigin.x - rhsOrigin.x;
			const float deltaY = lhsOrigin.y - rhsOrigin.y;
			const float deltaZ = lhsOrigin.z - rhsOrigin.z;
			const float originDistance =
				std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
			return maxRowDelta > rowTolerance || originDistance > originTolerance;
		}

		void BeginAnchorFrame(std::uint32_t a_frame) noexcept
		{
			if (g_anchorFrame.frameCount == a_frame) {
				return;
			}
			if (g_anchorFrame.frameCount != std::numeric_limits<std::uint32_t>::max()) {
				g_completedFrames.fetch_add(1, std::memory_order_relaxed);
				if (g_worldSnapshot.valid
					&& g_worldSnapshot.frameCount == g_anchorFrame.frameCount) {
					g_publicationFrames.fetch_add(1, std::memory_order_relaxed);
				} else {
					g_noPublicationFrames.fetch_add(1, std::memory_order_relaxed);
				}
			}

			g_anchorFrame = { .frameCount = a_frame };
			g_fullscreenLightAnchorsThisFrame.store(0, std::memory_order_relaxed);
			g_publicationsThisFrame.store(0, std::memory_order_relaxed);
			g_rejectionsThisFrame.store(0, std::memory_order_relaxed);
			g_distinctCamerasThisFrame.store(0, std::memory_order_relaxed);
			g_lastRejectReason.store(
				FrameBufferRejectReason::kNone,
				std::memory_order_relaxed);
			g_worldSnapshot = {};
			g_worldSnapshot.frameCount = a_frame;
		}

		void Reject(FrameBufferRejectReason a_reason) noexcept
		{
			g_rejectionsThisFrame.fetch_add(1, std::memory_order_relaxed);
			g_lastRejectReason.store(a_reason, std::memory_order_relaxed);
		}

		[[nodiscard]] FrameBufferRejectReason ValidateLatestSnapshot(
			std::uint32_t a_frame) noexcept
		{
			if (!g_latestSnapshot.valid) {
				return FrameBufferRejectReason::kMissingSnapshot;
			}
			if (g_latestSnapshot.frameCount != a_frame) {
				return FrameBufferRejectReason::kStaleSnapshot;
			}
			if (g_byteWidth.load(std::memory_order_relaxed) < kMinimumByteWidth) {
				return FrameBufferRejectReason::kBufferTooSmall;
			}
			if (!HasUsableCameraBasis(g_latestSnapshot.data)) {
				return FrameBufferRejectReason::kInvalidCameraBasis;
			}
			if (!HasFiniteWorldToClip(g_latestSnapshot.data)) {
				return FrameBufferRejectReason::kInvalidProjection;
			}
			const auto origin = CameraWorldOrigin(g_latestSnapshot.data);
			const auto previousOrigin =
				CameraPreviousWorldOrigin(g_latestSnapshot.data);
			if (!std::isfinite(origin.x)
				|| !std::isfinite(origin.y)
				|| !std::isfinite(origin.z)
				|| !std::isfinite(previousOrigin.x)
				|| !std::isfinite(previousOrigin.y)
				|| !std::isfinite(previousOrigin.z)) {
				return FrameBufferRejectReason::kInvalidOrigin;
			}
			if (!HasNonzeroWorldCameraOrigin(g_latestSnapshot.data)) {
				return FrameBufferRejectReason::kNearZeroOrigin;
			}
			return FrameBufferRejectReason::kNone;
		}

		void PublishAtFullscreenLightDraw() noexcept
		{
			const auto frame = CurrentEngineFrame();
			BeginAnchorFrame(frame);
			g_fullscreenLightAnchorsThisFrame.fetch_add(1, std::memory_order_relaxed);

			const auto rejection = ValidateLatestSnapshot(frame);
			if (rejection != FrameBufferRejectReason::kNone) {
				if (rejection == FrameBufferRejectReason::kNearZeroOrigin) {
					g_nearZeroOriginRejections.fetch_add(1, std::memory_order_relaxed);
				}
				Reject(rejection);
				return;
			}

			if (!g_anchorFrame.lastCamera.valid
				|| CameraMateriallyDiffers(
					g_latestSnapshot.data,
					g_anchorFrame.lastCamera.data)) {
				g_anchorFrame.lastCamera = g_latestSnapshot;
				g_distinctCamerasThisFrame.fetch_add(1, std::memory_order_relaxed);
			}
			g_anchorFrame.fullscreenLight = g_latestSnapshot;
			g_anchorFrame.fullscreenLight.source =
				FrameBufferPublishSource::kFullscreenLightDraw;

			const bool sameSnapshot =
				g_worldSnapshot.valid
				&& g_worldSnapshot.frameCount == frame
				&& g_worldSnapshot.sequence == g_latestSnapshot.sequence;
			if (sameSnapshot) {
				return;
			}

			g_worldSnapshot = g_latestSnapshot;
			g_worldSnapshot.source = FrameBufferPublishSource::kFullscreenLightDraw;
			g_worldSnapshot.valid = true;
			g_publicationsThisFrame.fetch_add(1, std::memory_order_relaxed);
			g_lastRejectReason.store(
				FrameBufferRejectReason::kNone,
				std::memory_order_relaxed);
		}

		void BeginDeferredLights() noexcept
		{
			BeginAnchorFrame(CurrentEngineFrame());
		}
	}

	void InstallFrameBuffer()
	{
		if (g_installed) {
			return;
		}
		g_installed = true;

		const bool registered = RegisterPostDeferredPrePass(
			ResolveIdentity,
			HookPriority::Late);
		if (!registered) {
			L->error(
				"Per-frame constant buffer identity resolution disabled: "
				"pre-pass hook registration failed.");
		}
		RegisterPreDeferredLightsImpl(BeginDeferredLights, HookPriority::Early);
		RegisterPreFullscreenDeferredLightDraw(
			PublishAtFullscreenLightDraw,
			HookPriority::Early);
	}

	void OnFrameBufferD3D11Ready(ID3D11DeviceContext* a_context)
	{
		if (!a_context || g_hookInstalled.load(std::memory_order_relaxed)) {
			return;
		}

		AdoptIdentity(EnginePerFrameBuffer(), "context");
		stl::detour_vfunc<14, FrameBufferMap_Hook>(a_context);
		stl::detour_vfunc<15, FrameBufferUnmap_Hook>(a_context);
		if (!FrameBufferMap_Hook::func || !FrameBufferUnmap_Hook::func) {
			L->error("Context-vtable hooks for Map/Unmap have no original; snapshot disabled.");
			return;
		}

		g_hookedContext.store(a_context, std::memory_order_relaxed);
		g_hookInstalled.store(true, std::memory_order_relaxed);
		L->info("Context-vtable hooks installed on Map (slot 14) and Unmap (slot 15).");
	}

	const FrameBufferSnapshot& GetFrameBuffer() noexcept
	{
		return g_worldSnapshot;
	}

	const FrameBufferSnapshot& GetLatestFrameBuffer() noexcept
	{
		return g_latestSnapshot;
	}

	FrameBufferStatus GetFrameBufferStatus() noexcept
	{
		FrameBufferStatus status;
		const auto frame = CurrentEngineFrame();
		status.hookInstalled = g_hookInstalled.load(std::memory_order_relaxed);
		status.hookedContextIsCurrent =
			g_hookedContextIsCurrent.load(std::memory_order_relaxed);
		status.identified = g_identity.load(std::memory_order_relaxed) != nullptr;
		status.identitySource = g_identitySource.load(std::memory_order_relaxed);
		status.contextMatchesSlot12 =
			g_contextMatchesSlot12.load(std::memory_order_relaxed);
		status.byteWidth = g_byteWidth.load(std::memory_order_relaxed);
		status.usage = g_usage.load(std::memory_order_relaxed);
		status.cpuAccessFlags = g_cpuAccessFlags.load(std::memory_order_relaxed);
		status.bindFlags = g_bindFlags.load(std::memory_order_relaxed);
		status.snapshots = g_snapshots.load(std::memory_order_relaxed);
		status.mapsLastFrame = g_mapsLastFrame.load(std::memory_order_relaxed);
		status.maxMapsPerFrame = g_maxMapsPerFrame.load(std::memory_order_relaxed);
		status.latestSnapshotValid = g_latestSnapshot.valid;
		status.publishedSnapshotValid = g_worldSnapshot.valid;
		status.publishedThisFrame =
			g_worldSnapshot.valid && g_worldSnapshot.frameCount == frame;
		status.latestFrameCount = g_latestSnapshot.frameCount;
		status.publishedFrameCount = g_worldSnapshot.frameCount;
		status.fullscreenLightAnchorsThisFrame =
			g_fullscreenLightAnchorsThisFrame.load(std::memory_order_relaxed);
		status.publicationsThisFrame =
			g_publicationsThisFrame.load(std::memory_order_relaxed);
		status.rejectionsThisFrame =
			g_rejectionsThisFrame.load(std::memory_order_relaxed);
		status.distinctCamerasThisFrame =
			g_distinctCamerasThisFrame.load(std::memory_order_relaxed);
		status.minimumOriginMagnitude = kMinimumWorldCameraOriginMagnitude;
		status.latestSequence = g_latestSnapshot.sequence;
		status.publishedSequence = g_worldSnapshot.sequence;
		status.publishedCameraHash =
			g_worldSnapshot.valid ? HashCamera(g_worldSnapshot.data) : 0;
		status.completedFrames =
			g_completedFrames.load(std::memory_order_relaxed);
		status.publicationFrames =
			g_publicationFrames.load(std::memory_order_relaxed);
		status.noPublicationFrames =
			g_noPublicationFrames.load(std::memory_order_relaxed);
		status.nearZeroOriginRejections =
			g_nearZeroOriginRejections.load(std::memory_order_relaxed);
		status.publishSource = g_worldSnapshot.source;
		status.lastRejectReason =
			g_lastRejectReason.load(std::memory_order_relaxed);
		status.fullscreenLight = g_anchorFrame.fullscreenLight;
		return status;
	}

	const char* FrameBufferPublishSourceName(
		FrameBufferPublishSource a_source) noexcept
	{
		switch (a_source) {
		case FrameBufferPublishSource::kFullscreenLightDraw:
			return "fullscreen_light_draw";
		default:
			return "none";
		}
	}

	const char* FrameBufferRejectReasonName(
		FrameBufferRejectReason a_reason) noexcept
	{
		switch (a_reason) {
		case FrameBufferRejectReason::kMissingSnapshot:
			return "missing_snapshot";
		case FrameBufferRejectReason::kStaleSnapshot:
			return "stale_snapshot";
		case FrameBufferRejectReason::kBufferTooSmall:
			return "buffer_too_small";
		case FrameBufferRejectReason::kInvalidCameraBasis:
			return "invalid_camera_basis";
		case FrameBufferRejectReason::kInvalidProjection:
			return "invalid_projection";
		case FrameBufferRejectReason::kInvalidOrigin:
			return "invalid_origin";
		case FrameBufferRejectReason::kNearZeroOrigin:
			return "near_zero_origin";
		default:
			return "none";
		}
	}
}
