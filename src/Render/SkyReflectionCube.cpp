#include "Render/SkyReflectionCube.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/EngineCallSite.h"
#include "Render/FrameBuffer.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "Render/RuntimeCallback.h"
#include "World/Sky.h"
#include "World/Weather.h"

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
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
		// Beyond this the eye has left the dome the faces came from.
		constexpr float kEyeJumpLimit = 512.0f;
		constexpr std::uint32_t kSideFaces = 0xF;
		constexpr std::uint32_t kPolarFirstFace = 4;
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

		// Upstream uses 256; FO4 object LOD is sub-texel there.
		std::atomic<std::uint32_t> g_cubeSize{ 256 };

		// The only cube creation, inside BSShaderRenderTargets::Create.
		constexpr CallSiteAnchor kCubeTargetCreate{
			.name = "BSShaderRenderTargets::Create -> RenderTargetManager::CreateCubeMapRenderTarget",
			.function = RE::ID::BSShaderRenderTargets::Create,
			.offset = { 0xDCB, 0xE21, 0xE21 },
			.target = RE::ID::BSGraphics::RenderTargetManager::CreateCubeMapRenderTarget
		};

		struct CreateCubeMapRenderTarget_Hook
		{
			static void thunk(
				RE::BSGraphics::RenderTargetManager* a_this,
				RE::BSGraphics::CubeMapRenderTargetID a_logicalID,
				RE::BSGraphics::CubeMapRenderTargetProperties* a_properties,
				RE::BSGraphics::RenderTargetCreationMode a_mode)
			{
				const auto size = g_cubeSize.load(std::memory_order_relaxed);
				auto& properties = *a_properties;
				properties.width = size;
				properties.height = size;
				func(a_this, a_logicalID, a_properties, a_mode);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The face depth must be at least the face size; it only grows.
		struct CreateDepthStencilTarget_Hook
		{
			static void thunk(
				RE::BSGraphics::RenderTargetManager* a_this,
				RE::BSGraphics::DepthStencilTargetID a_logicalID,
				RE::BSGraphics::DepthStencilTargetCreationProperties* a_properties,
				RE::BSGraphics::RenderTargetCreationMode a_mode)
			{
				if (a_logicalID == RE::BSGraphics::DepthStencilTargetID::kCubemapReflections) {
					const auto size = g_cubeSize.load(std::memory_order_relaxed);
					// FO4: edit only the common prefix; NG/AE creation tails stay untouched.
					auto& properties = a_properties->properties;
					properties.width = std::max(properties.width, size);
					properties.height = std::max(properties.height, size);
				}
				func(a_this, a_logicalID, a_properties, a_mode);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void InstallCubeSizeHook()
		{
			const auto site = ResolveCallSite(kCubeTargetCreate);
			if (!site) {
				L->warn("Engine cube stays 512x512: {}", site.error());
				return;
			}
			stl::write_thunk_call<CreateCubeMapRenderTarget_Hook>(*site);
			stl::detour_thunk<CreateDepthStencilTarget_Hook>(RE::ID::BSGraphics::RenderTargetManager::CreateDepthStencilTarget);
		}

		struct Consumer
		{
			std::string name;
			std::function<bool()> wantsCube;
			std::function<bool()> wantsLod;
			Feature* owner;
		};

		struct Counters
		{
			std::atomic<std::uint64_t> renders{ 0 };
			std::atomic<std::uint64_t> faces{ 0 };
			std::atomic<std::uint64_t> lodFaces{ 0 };
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
		std::uint32_t g_polarNext = 0;
		std::uint32_t g_sideNext = 0;
		std::uint32_t g_sidePending = kSideFaces;
		bool g_lastLod = false;
		std::optional<RE::NiPoint3> g_lastEye;
		std::optional<RE::NiPoint3> g_lodEye;
		const void* g_lodWorldSpace = nullptr;
		WeatherSnapshot g_lodWeather;
		std::optional<std::uint32_t> g_lastFrame;
		// ClickCubeMap selects front culling (2) and exits with back (1).
		constexpr auto kCubeCullMode = static_cast<RE::BSGraphics::RasterStateCullMode>(2);
		std::optional<SkyReflectionCubeCapture> g_capture;
		std::uint64_t g_captureSerial = 0;

		class ScopedCapture
		{
		public:
			explicit ScopedCapture(std::uint32_t a_face) noexcept { g_capture = { a_face, ++g_captureSerial }; }
			~ScopedCapture() { g_capture.reset(); }

			ScopedCapture(const ScopedCapture&) = delete;
			ScopedCapture& operator=(const ScopedCapture&) = delete;
		};

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
			g_polarNext = 0;
			g_sideNext = 0;
			g_sidePending = kSideFaces;
			g_lastEye.reset();
			g_lodEye.reset();
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
			return player && player->parentCell && RE::UI::GetSingleton() && !IsMainOrLoadingMenuOpen();
		}

		[[nodiscard]] bool AnyConsumerWants(
			std::function<bool()> Consumer::* a_callback,
			std::string_view a_phase) noexcept
		{
			return std::ranges::any_of(g_consumers, [a_callback, a_phase](const Consumer& a_consumer) {
				const auto& callback = a_consumer.*a_callback;
				if (!callback)
					return false;
				bool wants = false;
				return render::RunRenderCallback(a_consumer.owner, a_phase, [&] {
					wants = callback();
				}) && wants;
			});
		}

		[[nodiscard]] bool AnyConsumerWantsCube() noexcept
		{
			return AnyConsumerWants(&Consumer::wantsCube, "SkyReflectionCube::wantsCube");
		}

		[[nodiscard]] bool AnyConsumerWantsLod() noexcept
		{
			return AnyConsumerWants(&Consumer::wantsLod, "SkyReflectionCube::wantsLod");
		}

		[[nodiscard]] RE::BSGraphics::CubeMapRenderTarget* GetEngineCube() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			auto* manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
			if (!rendererData || !manager)
				return nullptr;
			const auto platformID = manager->GetCubeMapRenderTargetPlatformID(
				static_cast<std::size_t>(RE::BSGraphics::CubeMapRenderTargetID::kReflections));
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
			constexpr auto kUnbound = RE::BSGraphics::RenderTargetID::kNone;
			for (std::int32_t slot = 1; slot < static_cast<std::int32_t>(std::size(a_state.renderTargets)); ++slot) {
				if (a_state.renderTargets[slot] == static_cast<std::int32_t>(kUnbound))
					continue;
				a_manager.SetCurrentRenderTarget(slot, static_cast<std::int32_t>(kUnbound), RE::BSGraphics::SetRenderTargetMode::kNoClear);
				g_counters.unboundTargets.fetch_add(1, std::memory_order_relaxed);
			}
		}

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

		// Forward Lighting ships no LOD bases (8, 9, 13, 18, 19).
		constexpr std::array kLodFlags{
			RE::BSShaderProperty::EShaderPropertyFlag::kLODLandscape,
			RE::BSShaderProperty::EShaderPropertyFlag::kLODObjects,
			RE::BSShaderProperty::EShaderPropertyFlag::kLODLandBlend
		};
		// Cleared with them: it would select the missing base 8.
		constexpr auto kLandscapeBlendFlag = RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape;
		constexpr std::uint32_t kMaxLodDepth = 16;

		// Side-face view axes (forward, up, right); the engine's face basis table.
		struct FaceBasis
		{
			RE::NiPoint3 forward, up, right;
		};
		constexpr std::array<FaceBasis, 4> kSideBasis{ {
			{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } },
			{ { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
			{ { 0, 1, 0 }, { 0, 0, -1 }, { 1, 0, 0 } },
			{ { 0, -1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
		} };

		// Side planes of the face pyramid; near and far are left to the engine.
		class FaceFrustum
		{
		public:
			[[nodiscard]] static std::optional<FaceFrustum> Make(const CubeCamera& a_camera, std::uint32_t a_face)
			{
				if (a_face >= kSideBasis.size())
					return std::nullopt;
				const auto& f = a_camera.viewFrustum;
				// Widest edge keeps the test conservative for any sign convention.
				const float slope = std::max({ std::abs(f.left), std::abs(f.right), std::abs(f.top), std::abs(f.bottom) });
				if (f.ortho || !std::isfinite(slope) || slope <= 0.0f)
					return std::nullopt;
				return FaceFrustum{ a_camera.local.translate, kSideBasis[a_face], slope };
			}

			// Inclusive; bounds the engine cannot cull on count as visible.
			[[nodiscard]] bool Sees(const RE::NiBound& a_bound) const noexcept
			{
				const float radius = a_bound.fRadius;
				if (!std::isfinite(radius) || radius <= 0.0f)
					return true;
				const auto offset = a_bound.center - _eye;
				const float reach = _slope * offset.Dot(_basis.forward) + radius * std::sqrt(1.0f + _slope * _slope);
				return std::abs(offset.Dot(_basis.right)) <= reach && std::abs(offset.Dot(_basis.up)) <= reach;
			}

		private:
			FaceFrustum(const RE::NiPoint3& a_eye, const FaceBasis& a_basis, float a_slope) noexcept :
				_eye(a_eye), _basis(a_basis), _slope(a_slope)
			{}

			RE::NiPoint3 _eye;
			FaceBasis _basis;
			float _slope;
		};

		// Clearing the LOD flags makes mode 0 resolve the base technique.
		class ScopedLodLightingFlags
		{
		public:
			explicit ScopedLodLightingFlags(const std::optional<FaceFrustum>& a_frustum) noexcept
			{
				const FaceFrustum* frustum = a_frustum ? &*a_frustum : nullptr;
				const auto* land = RE::BGSTerrainManager::GetLandNode();
				const auto* objects = RE::BGSTerrainManager::GetObjectsNode();
				if (!land && !objects) {
					CS_LOG_ONCE(L, spdlog::level::warn, "No LOD land or objects root; LOD flags stay as authored.");
					return;
				}
				try {
					Visit(land, 0, frustum);
					Visit(objects, 0, frustum);
				} catch (...) {
					Restore();
					CS_LOG_ONCE(L, spdlog::level::warn, "LOD flag clearing failed; flags were restored.");
				}
			}

			~ScopedLodLightingFlags() { Restore(); }

			ScopedLodLightingFlags(const ScopedLodLightingFlags&) = delete;
			ScopedLodLightingFlags& operator=(const ScopedLodLightingFlags&) = delete;

		private:
			struct Change
			{
				RE::NiPointer<RE::BSShaderProperty> property;
				std::uint64_t bits;
			};

			void Visit(const RE::NiNode* a_node, std::uint32_t a_depth, const FaceFrustum* a_frustum)
			{
				if (!a_node || a_depth > kMaxLodDepth)
					return;
				for (const auto& child : a_node->children) {
					if (!child || child->GetAppCulled() || (a_frustum && !a_frustum->Sees(child->worldBound)))
						continue;
					if (const auto* node = child->IsNode()) {
						Visit(node, a_depth + 1, a_frustum);
					} else if (auto* geometry = child->IsGeometry()) {
						Clear(geometry);
					}
				}
			}

			void Clear(RE::BSGeometry* a_geometry)
			{
				auto* property = netimmerse_cast<RE::BSLightingShaderProperty*>(a_geometry->properties[1].get());
				if (!property)
					return;
				using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
				if (!property->flags.any(Flag::kLODLandscape, Flag::kLODObjects, Flag::kLODLandBlend))
					return;
				std::uint64_t bits = 0;
				for (const auto flag : kLodFlags) {
					if (property->flags.any(flag))
						bits |= static_cast<std::uint64_t>(flag);
				}
				if (property->flags.any(kLandscapeBlendFlag))
					bits |= static_cast<std::uint64_t>(kLandscapeBlendFlag);
				_changes.push_back({ RE::NiPointer<RE::BSShaderProperty>(property), bits });
				SetBits(*property, bits, false);
			}

			static void SetBits(RE::BSShaderProperty& a_property, std::uint64_t a_bits, bool a_value)
			{
				for (std::uint64_t bit = 1; bit != 0; bit <<= 1) {
					if (a_bits & bit)
						a_property.SetFlag(static_cast<RE::BSShaderProperty::EShaderPropertyFlag>(bit), a_value);
				}
			}

			void Restore() noexcept
			{
				for (auto& change : _changes)
					SetBits(*change.property, change.bits, true);
				_changes.clear();
			}

			std::vector<Change> _changes;
		};

		// Faces leave cube mode set; end it so no later draw lands in the cube.
		class ScopedCubeModeExit
		{
		public:
			explicit ScopedCubeModeExit(RE::BSGraphics::RenderTargetManager& a_manager) noexcept :
				_manager(a_manager)
			{}

			~ScopedCubeModeExit()
			{
				_manager.SetCurrentCubeMapRenderTarget(static_cast<std::int32_t>(RE::BSGraphics::CubeMapRenderTargetID::kNone), RE::BSGraphics::SetRenderTargetMode::kNoClear, 0);
			}

			ScopedCubeModeExit(const ScopedCubeModeExit&) = delete;
			ScopedCubeModeExit& operator=(const ScopedCubeModeExit&) = delete;

		private:
			RE::BSGraphics::RenderTargetManager& _manager;
		};

		// Full side refresh when the scene the LOD was drawn from has changed.
		void QueueLodRefresh(const RE::NiPoint3& a_eye, bool a_lod)
		{
			const auto* tes = RE::TES::GetSingleton();
			const void* worldSpace = tes ? tes->worldSpace : nullptr;
			const auto weather = SnapshotWeather();
			const bool changed = a_lod != g_lastLod || !g_lodEye || g_lodEye->GetDistance(a_eye) > kEyeJumpLimit ||
			                     worldSpace != g_lodWorldSpace || weather.current != g_lodWeather.current ||
			                     weather.previous != g_lodWeather.previous;
			if (!changed)
				return;
			g_sidePending = kSideFaces;
			g_lastLod = a_lod;
			g_lodEye = a_eye;
			g_lodWorldSpace = worldSpace;
			g_lodWeather = weather;
		}

		// One side face per frame keeps the work constant; queued faces go first.
		[[nodiscard]] std::optional<std::uint32_t> NextSideFace() noexcept
		{
			g_sidePending |= 1u << g_sideNext;
			g_sideNext = (g_sideNext + 1) % static_cast<std::uint32_t>(std::popcount(kSideFaces));
			if (!g_sidePending)
				return std::nullopt;
			const auto face = static_cast<std::uint32_t>(std::countr_zero(g_sidePending));
			g_sidePending &= g_sidePending - 1;
			return face;
		}

		void ClickFace(CubeCamera& a_camera, RE::BSMultiBoundNode& a_skyRoot, std::uint32_t a_face, bool a_lod)
		{
			cs::render::annotation::ScopedEvent event(kFaceEvents[a_face]);
			std::optional<ScopedPreCullBypass> preCull;
			std::optional<ScopedLodLightingFlags> lodFlags;
			if (a_lod) {
				preCull.emplace();
				lodFlags.emplace(FaceFrustum::Make(a_camera, a_face));
				AddLodScenes(a_camera);
				g_counters.lodFaces.fetch_add(1, std::memory_order_relaxed);
			}
			a_camera.AddCubeMapScene(&a_skyRoot);
			// The release flag frees the scene list so it does not grow.
			{
				const ScopedCapture capture(a_face);
				a_camera.Click(static_cast<FaceFlags>(1u << a_face), false, true, false);
			}
			g_faceMask.fetch_or(1u << a_face, std::memory_order_relaxed);
			g_counters.faces.fetch_add(1, std::memory_order_relaxed);
		}

		// One polar face (sky only) and at most one side face per frame.
		void RenderFaces(
			CubeCamera& a_camera,
			RE::BSMultiBoundNode& a_skyRoot,
			RE::BSGraphics::RenderTargetManager& a_manager,
			RE::BSGraphics::RendererShadowState& a_state,
			const float (&a_clear)[4],
			const RE::NiPoint3& a_eye)
		{
			const ScopedSkyRootCubeFlag skyFlag(a_skyRoot);
			const ScopedClearColor clearColor(a_state, a_clear);
			const ScopedCubeModeExit cubeMode(a_manager);
			const bool lod = AnyConsumerWantsLod();
			QueueLodRefresh(a_eye, lod);
			ClickFace(a_camera, a_skyRoot, kPolarFirstFace + g_polarNext, false);
			g_polarNext ^= 1;
			if (const auto side = NextSideFace())
				ClickFace(a_camera, a_skyRoot, *side, lod);
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
			RenderFaces(**g_camera, *sky->root, *manager, context->shadowState, clear, eye);

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

	static bool RegisterConsumer(
		Feature* a_owner,
		std::string_view a_name,
		std::function<bool()> a_wantsCube,
		std::function<bool()> a_wantsLod)
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
			InstallCubeSizeHook();
			g_hookInstalled.store(true, std::memory_order_release);
		}
		g_consumers.push_back({ std::string(a_name), std::move(a_wantsCube), std::move(a_wantsLod), a_owner });
		L->info("Sky reflection cube consumer registered: {}", a_name);
		return true;
	}

	bool RegisterSkyReflectionCubeConsumer(
		std::string_view a_name,
		std::function<bool()> a_wantsCube,
		std::function<bool()> a_wantsLod)
	{
		return RegisterConsumer(nullptr, a_name, std::move(a_wantsCube), std::move(a_wantsLod));
	}

	bool RegisterSkyReflectionCubeConsumer(
		Feature& a_owner,
		std::string_view a_name,
		std::function<bool()> a_wantsCube,
		std::function<bool()> a_wantsLod)
	{
		return RegisterConsumer(&a_owner, a_name, std::move(a_wantsCube), std::move(a_wantsLod));
	}

	void ReselectSkyReflectionCubeCull() noexcept
	{
		if (!g_capture)
			return;
		auto* context = GetActiveContext();
		if (!context)
			return;
		// Mirrored face views need the selector ClickCubeMap writes inline.
		auto& state = context->shadowState;
		if (state.rasterStateCullMode.get() == kCubeCullMode)
			return;
		state.rasterStateCullMode = kCubeCullMode;
		state.stateUpdateFlags |= static_cast<std::uint32_t>(RE::BSGraphics::ShaderFlags::DIRTY_RASTERIZER_STATE);
	}

	ID3D11Texture2D* GetSkyReflectionCubeTexture() noexcept
	{
		const auto* cube = GetEngineCube();
		return cube ? reinterpret_cast<ID3D11Texture2D*>(cube->texture) : nullptr;
	}

	std::optional<SkyReflectionCubeCapture> GetSkyReflectionCubeCapture() noexcept
	{
		return g_capture;
	}

	void SetSkyReflectionCubeSize(std::uint32_t a_size) noexcept
	{
		g_cubeSize.store(a_size, std::memory_order_relaxed);
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
		status.lodFaces = g_counters.lodFaces.load(std::memory_order_relaxed);
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
