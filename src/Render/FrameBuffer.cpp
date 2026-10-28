#include "Render/FrameBuffer.h"

#include "Log.h"
#include "PCH.h"
#include "Render/Engine.h"
#include "Render/RenderHooks.h"
#include "Render/SharedData.h"

#include <cstring>
#include <mutex>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.framebuffer");
		bool g_installed = false;
		std::mutex g_prepassCameraMutex;
		std::optional<WorldCameraRecord> g_prepassCamera;
		std::optional<std::uint32_t> g_prepassFrame;

		void CaptureWorldCamera() noexcept
		{
			std::scoped_lock lock(g_prepassCameraMutex);
			const auto frame = GetFrameCount();
			if (g_prepassFrame != frame)
				render::LatchHistoryResetFrame();
			if (g_prepassFrame != frame || !g_prepassCamera) {
				g_prepassCamera = GetWorldCameraRecord();
				g_prepassFrame = frame;
			}
		}
	}

	void InstallFrameBuffer()
	{
		if (g_installed)
			return;
		g_installed = true;

		// MainRenderSetup publishes world+jitter before prepass material draws need b4.
		const auto priority = static_cast<HookPriority>(-200);
		const bool registered = RegisterPreDeferredPrePass(CaptureWorldCamera, priority) &&
		                        RegisterPostDeferredPrePass(CaptureWorldCamera, priority);
		if (!registered)
			L->error("World-camera capture disabled: pre-pass hook registration failed.");
	}

	std::optional<WorldCameraRecord> GetWorldCameraRecord() noexcept
	{
		const auto* state = GetGraphicsState();
		const auto* camera = RE::Main::WorldRootCamera();
		if (!state || !camera)
			return std::nullopt;
		// Cache +0x140 and record stride/key +0x250/+0x238/+0x240 match OG, NG and AE.
		static_assert(offsetof(RE::BSGraphics::State, cameraDataCache) == 0x140);
		static_assert(sizeof(RE::BSGraphics::CameraStateData) == 0x250);
		static_assert(offsetof(RE::BSGraphics::CameraStateData, referenceCamera) == 0x238);
		static_assert(offsetof(RE::BSGraphics::CameraStateData, useJitter) == 0x240);
		for (const auto& entry : state->cameraDataCache) {
			if (entry.referenceCamera != camera || !entry.useJitter)
				continue;
			WorldCameraRecord record{};
			std::memcpy(&record.View, entry.camViewData.viewMat, sizeof(record.View));
			std::memcpy(&record.Projection, entry.camViewData.projMat, sizeof(record.Projection));
			std::memcpy(&record.ViewProjection, entry.camViewData.viewProjMat, sizeof(record.ViewProjection));
			std::memcpy(&record.ViewProjectionUnjittered, entry.camViewData.viewProjUnjittered, sizeof(record.ViewProjectionUnjittered));
			std::memcpy(&record.PreviousViewProjectionUnjittered, entry.camViewData.previousViewProjUnjittered, sizeof(record.PreviousViewProjectionUnjittered));
			record.PosAdjust = { entry.posAdjust.x, entry.posAdjust.y, entry.posAdjust.z, 0 };
			record.PreviousPosAdjust = { entry.previousPosAdjust.x, entry.previousPosAdjust.y, entry.previousPosAdjust.z, 0 };
			record.frameCount = state->frameCount;
			return PrepareWorldCameraRecord(record) ? std::optional{ record } : std::nullopt;
		}
		return std::nullopt;
	}

	std::optional<WorldCameraRecord> GetCapturedWorldCameraRecord(
		std::optional<std::uint64_t> a_frame) noexcept
	{
		std::scoped_lock lock(g_prepassCameraMutex);
		if (!g_prepassCamera || (a_frame && g_prepassCamera->frameCount != *a_frame))
			return std::nullopt;
		return g_prepassCamera;
	}
}
