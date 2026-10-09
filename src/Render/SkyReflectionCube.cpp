#include "Render/SkyReflectionCube.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "World/Sky.h"

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <vector>

// windows.h defines far, which NiFrustum names a member.
#ifdef far
#	undef far
#endif

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.skyreflectioncube");

		using CubeCamera = RE::BSCubeMapCamera;
		using FaceFlags = CubeCamera::FaceEnableFlags;

		constexpr std::uint32_t kAllFaces = 0x3F;
		// Engine cadence: two faces per update, the full cube every third.
		constexpr std::uint32_t kFacesPerUpdate = 2;
		constexpr std::uint32_t kUpdatesPerCube = 3;
		// Beyond this the eye has left the dome the faces came from.
		constexpr float kEyeJumpLimit = 512.0f;
		// The eye is the previous render's camera; reject pre-load records.
		constexpr std::uint32_t kMaxCameraAgeFrames = 2;
		// The hook skips interiors and loads; a longer gap means stale faces.
		constexpr std::uint32_t kMaxRenderGapFrames = 1;
		// Update sets this on the sky root while it renders the cube.
		constexpr std::uint64_t kSkyRootCubeFlag = 1ull << 11;
		// Sky-Lower: the color below the dome.
		constexpr std::size_t kSkyLowerColor = 7;

		constexpr std::array<const char*, 6> kFaceEvents{
			"SkyReflectionCube::Face+X", "SkyReflectionCube::Face-X",
			"SkyReflectionCube::Face+Y", "SkyReflectionCube::Face-Y",
			"SkyReflectionCube::Face+Z", "SkyReflectionCube::Face-Z"
		};

		struct Consumer
		{
			std::string name;
			std::function<bool()> wantsCube;
		};

		struct Counters
		{
			std::atomic<std::uint64_t> renders{ 0 };
			std::atomic<std::uint64_t> faces{ 0 };
			std::atomic<std::uint64_t> invalidations{ 0 };
			std::atomic<std::uint64_t> unboundTargets{ 0 };
			std::atomic<std::uint64_t> skippedNoDemand{ 0 };
			std::atomic<std::uint64_t> skippedInterior{ 0 };
			std::atomic<std::uint64_t> skippedSkyHidden{ 0 };
			std::atomic<std::uint64_t> skippedNoCamera{ 0 };
			std::atomic<std::uint64_t> skippedNoTargets{ 0 };
			std::atomic<std::uint64_t> skippedNoWorld{ 0 };
		};

		std::vector<Consumer> g_consumers;
		std::atomic_bool g_hookInstalled{ false };
		std::atomic_bool g_disabled{ false };
		std::atomic_bool g_valid{ false };
		std::atomic<std::uint32_t> g_faceMask{ 0 };
		std::atomic<std::uint32_t> g_lastRenderFrame{ 0 };
		Counters g_counters;

		// Never destroyed: static teardown must not free into the game heap.
		RE::NiPointer<CubeCamera>* g_camera = nullptr;
		std::uint32_t g_step = 0;
		std::optional<RE::NiPoint3> g_lastEye;
		std::optional<std::uint32_t> g_lastFrame;

		void Disable(const char* a_reason) noexcept
		{
			if (!g_disabled.exchange(true, std::memory_order_acq_rel))
				L->error("Sky reflection cube disabled: {}", a_reason);
			g_valid.store(false, std::memory_order_release);
		}

		void Invalidate() noexcept
		{
			if (g_faceMask.exchange(0, std::memory_order_relaxed) != 0)
				g_counters.invalidations.fetch_add(1, std::memory_order_relaxed);
			g_valid.store(false, std::memory_order_release);
			g_step = 0;
			g_lastEye.reset();
		}

		[[nodiscard]] bool RenderIsCurrent(std::uint32_t a_frame) noexcept
		{
			return a_frame - g_lastRenderFrame.load(std::memory_order_relaxed) <= kMaxRenderGapFrames;
		}

		[[nodiscard]] bool IsCurrentlyValid(std::uint32_t a_frame) noexcept
		{
			return g_valid.load(std::memory_order_acquire) && RenderIsCurrent(a_frame);
		}

		// The main menu and loads reach the hook without a loaded world.
		[[nodiscard]] bool WorldLoaded() noexcept
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* ui = RE::UI::GetSingleton();
			return player && player->parentCell && ui && !ui->GetMenuOpen<RE::MainMenu>() &&
			       !ui->GetMenuOpen<RE::LoadingMenu>();
		}

		[[nodiscard]] bool AnyConsumerWantsCube()
		{
			return std::ranges::any_of(g_consumers, [](const Consumer& a_consumer) {
				return a_consumer.wantsCube();
			});
		}

		[[nodiscard]] RE::BSGraphics::CubeMapRenderTarget* GetEngineCube() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			auto* manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
			if (!rendererData || !manager)
				return nullptr;
			const auto platformID = manager->GetCubeMapRenderTargetPlatformID(0);
			if (platformID >= std::size(rendererData->cubeMapRenderTargets))
				return nullptr;
			auto& cube = rendererData->cubeMapRenderTargets[platformID];
			return cube.srView ? &cube : nullptr;
		}

		// Mirrors Renderer::SetClearColor and RestorePreviousClearColor.
		class ScopedClearColor
		{
		public:
			ScopedClearColor(RE::BSGraphics::RendererShadowState& a_state, const float (&a_color)[4]) noexcept :
				_state(a_state)
			{
				std::memcpy(_state.previousClearColor, _state.clearColor, sizeof(_state.clearColor));
				std::memcpy(_state.clearColor, a_color, sizeof(_state.clearColor));
			}

			~ScopedClearColor()
			{
				std::memcpy(_state.clearColor, _state.previousClearColor, sizeof(_state.clearColor));
			}

			ScopedClearColor(const ScopedClearColor&) = delete;
			ScopedClearColor& operator=(const ScopedClearColor&) = delete;

		private:
			RE::BSGraphics::RendererShadowState& _state;
		};

		class ScopedSkyRootCubeFlag
		{
		public:
			explicit ScopedSkyRootCubeFlag(RE::NiAVObject& a_root) noexcept :
				_root(a_root),
				_wasSet((a_root.flags.flags & kSkyRootCubeFlag) != 0)
			{
				_root.flags.flags |= kSkyRootCubeFlag;
			}

			~ScopedSkyRootCubeFlag()
			{
				if (!_wasSet)
					_root.flags.flags &= ~kSkyRootCubeFlag;
			}

			ScopedSkyRootCubeFlag(const ScopedSkyRootCubeFlag&) = delete;
			ScopedSkyRootCubeFlag& operator=(const ScopedSkyRootCubeFlag&) = delete;

		private:
			RE::NiAVObject& _root;
			bool _wasSet;
		};

		// ClickCubeMap sets only RT slot 0; sky shaders write a second output.
		void UnbindAuxiliaryTargets(
			RE::BSGraphics::RenderTargetManager& a_manager,
			const RE::BSGraphics::RendererShadowState& a_state) noexcept
		{
			constexpr std::int32_t kUnbound = -1;
			for (std::int32_t slot = 1; slot < static_cast<std::int32_t>(std::size(a_state.renderTargets)); ++slot) {
				if (a_state.renderTargets[slot] == kUnbound)
					continue;
				a_manager.SetCurrentRenderTarget(slot, kUnbound, RE::BSGraphics::SetRenderTargetMode::kNoClear);
				g_counters.unboundTargets.fetch_add(1, std::memory_order_relaxed);
			}
		}

		// Engine Update adds these only when the worldspace has a terrain manager.
		void AddLodScenes(CubeCamera& a_camera)
		{
			for (auto* node : { RE::BGSTerrainManager::GetLandNode(),
					 RE::BGSTerrainManager::GetObjectsNode(),
					 RE::BGSTerrainManager::GetTreesNode() }) {
				if (node)
					a_camera.AddCubeMapScene(node);
			}
		}

		// Faces leave cube mode set; end it so no later draw lands in the cube.
		class ScopedCubeModeExit
		{
		public:
			explicit ScopedCubeModeExit(RE::BSGraphics::RenderTargetManager& a_manager) noexcept :
				_manager(a_manager)
			{}

			~ScopedCubeModeExit()
			{
				_manager.SetCurrentCubeMapRenderTarget(-1, RE::BSGraphics::SetRenderTargetMode::kNoClear, 0);
			}

			ScopedCubeModeExit(const ScopedCubeModeExit&) = delete;
			ScopedCubeModeExit& operator=(const ScopedCubeModeExit&) = delete;

		private:
			RE::BSGraphics::RenderTargetManager& _manager;
		};

		[[nodiscard]] bool EnsureCamera()
		{
			if (g_camera)
				return true;
			auto camera = CubeCamera::Create();
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::BSCubeMapCamera[0] };
			if (!camera || *reinterpret_cast<const std::uintptr_t*>(camera.get()) != vtable.address()) {
				Disable("BSCubeMapCamera construction did not produce the expected object");
				return false;
			}
			g_camera = new RE::NiPointer<CubeCamera>(std::move(camera));
			return true;
		}

		// Engine setup: 90 degree frustum out to the world far plane.
		void SetupCamera(CubeCamera& a_camera, const RE::NiPoint3& a_eye, float a_farPlane)
		{
			a_camera.local.translate = a_eye;
			auto frustum = a_camera.viewFrustum;
			frustum.far = a_farPlane;
			a_camera.SetViewFrustum(frustum);
		}

		void RenderFaces(
			CubeCamera& a_camera,
			RE::BSMultiBoundNode& a_skyRoot,
			RE::BSGraphics::RenderTargetManager& a_manager,
			RE::BSGraphics::RendererShadowState& a_state,
			const float (&a_clear)[4])
		{
			const ScopedSkyRootCubeFlag skyFlag(a_skyRoot);
			const ScopedClearColor clearColor(a_state, a_clear);
			const ScopedCubeModeExit cubeMode(a_manager);
			AddLodScenes(a_camera);
			a_camera.AddCubeMapScene(&a_skyRoot);
			const std::array<std::uint32_t, kFacesPerUpdate> faces{ g_step, 5 - g_step };
			for (std::size_t i = 0; i < faces.size(); ++i) {
				cs::render::annotation::ScopedEvent event(kFaceEvents[faces[i]]);
				// The last call releases the scene list so it does not grow.
				a_camera.Click(static_cast<FaceFlags>(1u << faces[i]), false, i + 1 == faces.size(), false);
				g_faceMask.fetch_or(1u << faces[i], std::memory_order_relaxed);
				g_counters.faces.fetch_add(1, std::memory_order_relaxed);
			}
			g_step = (g_step + 1) % kUpdatesPerCube;
		}

		void Render()
		{
			if (g_disabled.load(std::memory_order_acquire))
				return;
			auto* state = GetGraphicsState();
			if (!state)
				return;
			if (g_lastFrame && *g_lastFrame == state->frameCount)
				return;
			g_lastFrame = state->frameCount;

			if (!RenderIsCurrent(state->frameCount))
				Invalidate();
			if (!WorldLoaded()) {
				g_counters.skippedNoWorld.fetch_add(1, std::memory_order_relaxed);
				Invalidate();
				return;
			}

			if (!AnyConsumerWantsCube()) {
				g_counters.skippedNoDemand.fetch_add(1, std::memory_order_relaxed);
				Invalidate();
				return;
			}
			if (IsInterior()) {
				g_counters.skippedInterior.fetch_add(1, std::memory_order_relaxed);
				Invalidate();
				return;
			}
			if (IsSkyHidden()) {
				g_counters.skippedSkyHidden.fetch_add(1, std::memory_order_relaxed);
				Invalidate();
				return;
			}

			auto* sky = RE::Sky::GetSingleton();
			auto* worldCamera = RE::Main::WorldRootCamera();
			auto* manager = GetRenderTargetManager();
			auto* context = GetActiveContext();
			if (!sky || !sky->root || !worldCamera || !manager || !context || !GetEngineCube()) {
				g_counters.skippedNoTargets.fetch_add(1, std::memory_order_relaxed);
				Invalidate();
				return;
			}

			// This frame's camera is published later; use the previous render's.
			const auto record = GetCapturedWorldCameraRecord();
			if (!record || state->frameCount - record->frameCount > kMaxCameraAgeFrames) {
				g_counters.skippedNoCamera.fetch_add(1, std::memory_order_relaxed);
				Invalidate();
				return;
			}
			const auto origin = CameraWorldOrigin(*record);
			const RE::NiPoint3 eye{ origin.x, origin.y, origin.z };
			if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)) {
				g_counters.skippedNoCamera.fetch_add(1, std::memory_order_relaxed);
				Invalidate();
				return;
			}
			if (g_lastEye && g_lastEye->GetDistance(eye) > kEyeJumpLimit)
				Invalidate();
			g_lastEye = eye;

			if (!EnsureCamera())
				return;

			const auto& color = sky->skyColor[kSkyLowerColor];
			const float clear[4]{
				std::pow(color.r, 2.2f), std::pow(color.g, 2.2f), std::pow(color.b, 2.2f), 0.0f
			};

			cs::render::annotation::ScopedEvent container("SkyReflectionCube", false);
			std::optional<UnscaledRenderScope> unscaled;
			if (manager->IsDynamicResolutionCurrentlyActivated())
				unscaled.emplace();
			UnbindAuxiliaryTargets(*manager, context->shadowState);
			SetupCamera(**g_camera, eye, worldCamera->viewFrustum.far);
			RenderFaces(**g_camera, *sky->root, *manager, context->shadowState, clear);

			g_counters.renders.fetch_add(1, std::memory_order_relaxed);
			g_lastRenderFrame.store(state->frameCount, std::memory_order_relaxed);
			if (g_faceMask.load(std::memory_order_relaxed) == kAllFaces)
				g_valid.store(true, std::memory_order_release);
		}

		void RenderGuarded() noexcept
		{
			try {
				Render();
			} catch (const std::exception& e) {
				Disable(e.what());
			} catch (...) {
				Disable("non-standard exception");
			}
		}
	}

	bool RegisterSkyReflectionCubeConsumer(std::string_view a_name, std::function<bool()> a_wantsCube)
	{
		if (!RenderHookRegistrationAllowed("SkyReflectionCubeConsumer"))
			return false;
		if (g_disabled.load(std::memory_order_acquire))
			return false;
		if (!g_hookInstalled.load(std::memory_order_acquire)) {
			if (!RegisterPreWaterUpdate(&RenderGuarded)) {
				Disable("the water-update render hook could not be installed");
				return false;
			}
			g_hookInstalled.store(true, std::memory_order_release);
		}
		g_consumers.push_back({ std::string(a_name), std::move(a_wantsCube) });
		L->info("Sky reflection cube consumer registered: {}", a_name);
		return true;
	}

	ID3D11ShaderResourceView* GetSkyReflectionCubeSRV() noexcept
	{
		const auto* state = GetGraphicsState();
		if (!state || !IsCurrentlyValid(state->frameCount))
			return nullptr;
		const auto* cube = GetEngineCube();
		return cube ? reinterpret_cast<ID3D11ShaderResourceView*>(cube->srView) : nullptr;
	}

	SkyReflectionCubeStatus GetSkyReflectionCubeStatus() noexcept
	{
		SkyReflectionCubeStatus status;
		status.hookInstalled = g_hookInstalled.load(std::memory_order_relaxed);
		status.disabled = g_disabled.load(std::memory_order_relaxed);
		const auto* state = GetGraphicsState();
		status.valid = state && IsCurrentlyValid(state->frameCount);
		status.faceMask = g_faceMask.load(std::memory_order_relaxed);
		status.consumers = static_cast<std::uint32_t>(g_consumers.size());
		status.renders = g_counters.renders.load(std::memory_order_relaxed);
		status.faces = g_counters.faces.load(std::memory_order_relaxed);
		status.invalidations = g_counters.invalidations.load(std::memory_order_relaxed);
		status.unboundTargets = g_counters.unboundTargets.load(std::memory_order_relaxed);
		status.skippedNoDemand = g_counters.skippedNoDemand.load(std::memory_order_relaxed);
		status.skippedInterior = g_counters.skippedInterior.load(std::memory_order_relaxed);
		status.skippedSkyHidden = g_counters.skippedSkyHidden.load(std::memory_order_relaxed);
		status.skippedNoCamera = g_counters.skippedNoCamera.load(std::memory_order_relaxed);
		status.skippedNoTargets = g_counters.skippedNoTargets.load(std::memory_order_relaxed);
		status.skippedNoWorld = g_counters.skippedNoWorld.load(std::memory_order_relaxed);
		return status;
	}
}
