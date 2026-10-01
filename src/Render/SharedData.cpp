#include "Render/SharedData.h"

#include "Feature.h"
#include "FeatureBuffer.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedDataLayout.h"
#include "Render/TemporalRenderer.h"
#include "Utils/CSBuffer.h"
#include "World/Sky.h"
#include "World/Water.h"

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <d3d11.h>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <winrt/base.h>

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shareddata");
		struct SubstrateData
		{
			FrameDataCB frame{};
			SharedDataCB shared{};
			SharedFeatureDataCB feature{};
			FO4SharedDataCB fo4{};
		};

		struct SubstrateState
		{
			std::array<winrt::com_ptr<ID3D11Buffer>, kSubstrateBufferCount> buffers;
			std::atomic_bool ready{ false };
			std::atomic_uint32_t lastFrame{ UINT32_MAX };
			std::uint32_t lastAttemptFrame = UINT32_MAX;
			FO4SharedDataCB fo4{};
			DirectX::XMFLOAT2 previousRatio{ 1.0f, 1.0f };
			bool hasResolutionHistory = false;
			// Render thread only.
			float timer = 0.0f;
			bool updateInstalled = false;
			bool updateInstallFailed = false;
			bool inDeferredLights = false;
			std::uint32_t debugFrame = UINT32_MAX;
			std::uint32_t debugSelectionFrame = UINT32_MAX;
			Feature* debugFeature = nullptr;
			winrt::com_ptr<ID3D11ShaderResourceView> debugTexture;
		};

		SubstrateState& GetSubstrateState()
		{
			static SubstrateState state;
			return state;
		}

		float Reciprocal(float a_value)
		{
			return a_value > 0.0f ? 1.0f / a_value : 0.0f;
		}

		float GetRealTimeDelta()
		{
			auto* timer = RE::BSTimer::GetSingleton();
			return timer ? timer->realTimeDelta : 0.0f;
		}

		SharedDataCB BuildSharedData(
			float a_timer,
			const engine::WorldCameraRecord& a_camera)
		{
			SharedDataCB data{};
			data.Timer = a_timer;

			auto* graphicsState = engine::GetGraphicsState();
			if (graphicsState) {
				const auto width = static_cast<float>(graphicsState->screenWidth);
				const auto height = static_cast<float>(graphicsState->screenHeight);
				data.BufferDim = { width, height, Reciprocal(width), Reciprocal(height) };
				data.FrameCount = TemporalRenderer::GetSingleton()->IsTemporalActive() ? graphicsState->frameCount : 0;
				data.FrameCountAlwaysActive = graphicsState->frameCount;
			}

			data.CameraData = engine::GetCameraDepthParameters(a_camera);
			data.MipBias = TemporalRenderer::GetSingleton()->GetMipBias();
			// FO4: heights and tile selection share b4's position-adjust anchor.
			engine::FillWaterData(data.WaterData, data.WaterSystemHeight,
				{ a_camera.CameraPosAdjust.x, a_camera.CameraPosAdjust.y, a_camera.CameraPosAdjust.z });

			float sunX = 0.0f;
			float sunY = 0.0f;
			float sunZ = 0.0f;
			if (engine::TryGetSunDirectionWS(sunX, sunY, sunZ)) {
				data.DirLightDirection = { -sunX, -sunY, -sunZ, 0.0f };
				data.SunDirection = data.DirLightDirection;
			}

			DirectX::XMFLOAT3 sunColor{};
			if (engine::TryGetSunLightColor(sunColor))
				data.DirLightColor = { sunColor.x, sunColor.y, sunColor.z, 1.0f };
			DirectX::XMFLOAT4 ambient[3]{};
			if (engine::TryGetDirectionalAmbientRows(ambient)) {
				data.AmbientSHR = PackAmbientSH(ambient[0]);
				data.AmbientSHG = PackAmbientSH(ambient[1]);
				data.AmbientSHB = PackAmbientSH(ambient[2]);
			}
			data.HideSky = engine::IsSkyHidden() ? 1u : 0u;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (const auto* cell = player ? player->GetParentCell() : nullptr)
				data.InInterior = cell->IsExterior() ? 0u : 1u;

			return data;
		}

		void PackFeatures(SubstrateData& a_data, const FeatureDataCB& a_features)
		{
			// FO4 HDR and sun radiance are linear; DALC alone requires its native power 2.2.
			a_data.feature.linearLightingSettings = {
				.enableLinearLighting = 1,
				.isDirLightLinear = 1,
				.dirLightMult = 1.0f,
				.lightGamma = 1.0f,
				.colorGamma = 1.0f,
				.emitColorGamma = 1.0f,
				.glowmapGamma = 1.0f,
				.ambientGamma = 2.2f,
				.fogGamma = 1.0f,
				.fogAlphaGamma = 1.0f,
				.effectGamma = 1.0f,
				.effectAlphaGamma = 1.0f,
				.skyGamma = 1.0f,
				.waterGamma = 1.0f,
				.vlGamma = 1.0f,
				.vanillaDiffuseColorMult = 1.0f,
				.directionalLightMult = 1.0f,
				.pointLightMult = 1.0f,
				.ambientMult = 1.0f,
				.emitColorMult = 1.0f,
				.glowmapMult = 1.0f,
				.effectLightingMult = 1.0f,
				.membraneEffectMult = 1.0f,
				.bloodEffectMult = 1.0f,
				.projectedEffectMult = 1.0f,
				.deferredEffectMult = 1.0f,
				.otherEffectMult = 1.0f
			};
			auto& fo4 = a_data.fo4;
			a_data.feature.exponentialHeightFogSettings = a_features.exponentialHeightFogSettings;
			fo4.EnabledSSR = a_features.dynamicCubemapsSettings.EnabledSSR;
			fo4.EnabledDynamicCubemaps = a_features.dynamicCubemapsSettings.Enabled;
			auto& terrain = a_data.feature.terraOccSettings;
			terrain.EnableTerrainShadow = a_features.terrainShadowsSettings.EnableTerrainShadow;
			std::ranges::copy(a_features.terrainShadowsSettings.Scale, terrain.Scale);
			std::ranges::copy(a_features.terrainShadowsSettings.ZRange, terrain.ZRange);
			std::ranges::copy(a_features.terrainShadowsSettings.Offset, terrain.Offset);
			terrain.ZBlur = a_features.terrainShadowsSettings.ZBlur;
			a_data.feature.wetnessEffectsSettings = a_features.wetnessEffectsSettings;
		}

		constexpr std::array<std::size_t, kSubstrateBufferCount> kBufferSizes{
			sizeof(FrameDataCB), sizeof(SharedDataCB), sizeof(SharedFeatureDataCB), sizeof(FO4SharedDataCB)
		};

		bool WriteSubstrate(ID3D11DeviceContext* a_context, const SubstrateData& a_data, std::size_t a_firstBuffer = 0) noexcept;

		bool WriteConstantBuffer(
			ID3D11DeviceContext* a_context,
			ID3D11Buffer* a_buffer,
			const void* a_data,
			std::size_t a_size) noexcept
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(a_context->Map(
					a_buffer,
					0,
					D3D11_MAP_WRITE_DISCARD,
					0,
					&mapped))) {
				return false;
			}
			std::memcpy(mapped.pData, a_data, a_size);
			a_context->Unmap(a_buffer, 0);
			return true;
		}

		ID3D11DeviceContext* GetImmediateContext() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			return rendererData ?
			           reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			           nullptr;
		}

		void UpdateSharedData(bool a_updateDepth = true) noexcept
		{
			auto& state = GetSubstrateState();
			if (!state.ready.load(std::memory_order_acquire))
				return;

			auto* graphicsState = engine::GetGraphicsState();
			if (!graphicsState)
				return;
			const auto frame = graphicsState->frameCount;
			const bool current = state.lastFrame.load(std::memory_order_relaxed) == frame;
			if (!a_updateDepth && (current || state.lastAttemptFrame == frame))
				return;
			auto* rendererData = RE::BSGraphics::GetRendererData();
			auto* context = rendererData ? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) : nullptr;
			const auto camera = engine::GetCapturedWorldCameraRecord(frame);
			if (!context || !camera)
				return;
			// Early draws can precede the camera capture; a miss must not poison the prepass attempt.
			state.lastAttemptFrame = frame;

			try {
				if (a_updateDepth)
					UpdateCanonicalDepth(context, *camera);
				SubstrateData data{};
				data.fo4 = state.fo4;
				PackFeatures(data, GetFeatureBufferData());
				if (current) {
					// Fog and terrain prepare after material draws; refresh b6/b7 without advancing camera history twice.
					if (!WriteSubstrate(context, data, kFeatureDataSlot - kFrameDataSlot))
						CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Shared feature constant-buffer map failed.");
					return;
				}
				const auto delta = GetRealTimeDelta();
				const auto nextTimer = state.timer + delta;
				data.shared = BuildSharedData(nextTimer, *camera);
				const auto* manager = engine::GetRenderTargetManager();
				const DirectX::XMFLOAT2 ratio{
					manager ? manager->GetDynamicWidthRatio() : 1.0f,
					manager ? manager->GetDynamicHeightRatio() : 1.0f
				};
				if (!(ratio.x > 0.0f) || !(ratio.y > 0.0f))
					return;
				const auto previousRatio = state.hasResolutionHistory ? state.previousRatio : ratio;
				const auto* clampTarget = engine::ResolveRenderTarget(engine::RenderTarget::kMainTemp);
				if (!clampTarget || !clampTarget->texture)
					return;
				D3D11_TEXTURE2D_DESC clampDesc{};
				reinterpret_cast<ID3D11Texture2D*>(clampTarget->texture)->GetDesc(&clampDesc);
				const float size = static_cast<float>(clampDesc.Width);
				if (!(size > 0.0f))
					return;
				const float clamp = REX::FModule::IsRuntimeOG() ? (std::trunc(size * ratio.x) - 1.0f) / size : ratio.x - 0.5f / size;
				data.frame = PackFrameData(*camera, ratio, previousRatio, ratio.x - clamp);
				data.fo4.DeltaTime = delta;
				if (!WriteSubstrate(context, data)) {
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::err,
						"Shared substrate constant-buffer map failed.");
					return;
				}
				state.timer = nextTimer;
				state.previousRatio = ratio;
				state.hasResolutionHistory = true;
				state.lastFrame.store(frame, std::memory_order_relaxed);
				state.debugFrame = UINT32_MAX;
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::err,
					"Shared substrate update failed: {}.",
					e.what());
			} catch (...) {
				CS_LOG_EVERY_MS(
					L,
					2000,
					spdlog::level::err,
					"Shared substrate update failed.");
			}
		}

		bool WriteSubstrate(ID3D11DeviceContext* a_context, const SubstrateData& a_data, std::size_t a_firstBuffer) noexcept
		{
			const void* sources[]{ &a_data.frame, &a_data.shared, &a_data.feature, &a_data.fo4 };
			auto& state = GetSubstrateState();
			for (std::size_t index = a_firstBuffer; index < kSubstrateBufferCount; ++index)
				if (!WriteConstantBuffer(a_context, state.buffers[index].get(), sources[index], kBufferSizes[index]))
					return false;
			state.fo4 = a_data.fo4;
			return true;
		}

		void UpdateFullscreenDebugData(ID3D11DeviceContext* a_context, bool a_refresh = false) noexcept
		{
			auto& state = GetSubstrateState();
			const auto* graphics = engine::GetGraphicsState();
			if (!graphics || !a_context || !IsSharedDataReady())
				return;
			if (state.debugFrame == graphics->frameCount && !a_refresh)
				return;
			if (state.debugSelectionFrame != graphics->frameCount) {
				state.debugFeature = FeatureManager::Get().GetFullscreenDebugFeature();
				state.debugSelectionFrame = graphics->frameCount;
			}
			auto debug = state.debugFeature ? state.debugFeature->GetFullscreenDebugData() : FullscreenDebugData{};
			if (debug.mode == 0)
				debug = {};
			auto fo4 = state.fo4;
			fo4.DebugOwner = debug.owner;
			fo4.DebugMode = debug.mode;
			fo4.DebugParams = { debug.params[0], debug.params[1], debug.params[2], debug.params[3] };
			// Producers can change debug resources between lighting and composite, not between draws.
			if (std::memcmp(&fo4, &state.fo4, sizeof(fo4)) != 0) {
				if (!WriteConstantBuffer(a_context, state.buffers[kFO4SharedDataSlot - kFrameDataSlot].get(), &fo4, sizeof(fo4))) {
					CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Shared debug constant-buffer map failed.");
					return;
				}
				state.fo4 = fo4;
			}
			state.debugTexture.copy_from(debug.texture);
			state.debugFrame = graphics->frameCount;
		}
	}

	void InitializeSharedData(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context)
	{
		auto& state = GetSubstrateState();
		if (state.ready.load(std::memory_order_acquire))
			return;
		if (state.updateInstallFailed)
			throw std::runtime_error("Shared substrate update hook installation failed.");
		if (!state.updateInstalled)
			return;
		if (!a_device || !a_context) {
			L->error("Shared substrate initialization skipped: no D3D11 device.");
			return;
		}

		const char* names[]{ "Render/FrameData", "Render/SharedData", "Render/FeatureData", "Render/FO4SharedData" };
		for (std::size_t index = 0; index < kSubstrateBufferCount; ++index) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = static_cast<UINT>(kBufferSizes[index]);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			DX::ThrowIfFailed(a_device->CreateBuffer(&desc, nullptr, state.buffers[index].put()));
			annotation::SetName(state.buffers[index].get(), names[index]);
		}
		InitializeCanonicalDepth(a_device);

		// engine state is unavailable during D3D bootstrap
		if (!WriteSubstrate(a_context, SubstrateData{})) {
			for (auto& buffer : state.buffers)
				buffer = nullptr;
			throw std::runtime_error("Shared substrate constant-buffer seeding failed.");
		}
		state.ready.store(true, std::memory_order_release);
		L->info(
			"Shared substrate ready: b4/b5/b6/b7 sizes={}/{}/{}/{} bytes; canonical depth at t17.",
			kBufferSizes[0], kBufferSizes[1], kBufferSizes[2], kBufferSizes[3]);
	}

	bool IsSharedDataReady() noexcept
	{
		const auto& state = GetSubstrateState();
		return state.updateInstalled && !state.updateInstallFailed && state.ready.load(std::memory_order_acquire);
	}

	void EnsureSharedDataUpdateInstalled()
	{
		auto& state = GetSubstrateState();
		if (state.updateInstalled || state.updateInstallFailed)
			return;
		const auto bind = [] {
			auto* context = GetImmediateContext();
			for (auto stage : { engine::ShaderStage::kVertex, engine::ShaderStage::kPixel, engine::ShaderStage::kCompute })
				BindSharedData(context, stage);
		};
		if (!engine::RegisterPreDeferredPrePass(
				[bind] {
					if (const auto* graphics = engine::GetGraphicsState())
						engine::BeginShaderInjectionFrame(graphics->frameCount);
					UpdateSharedData(false);
					bind();
					FeatureManager::Get().PrepassAll();
				},
				engine::HookPriority::Late) ||
			!engine::RegisterPostDeferredPrePass(
				[bind] { UpdateSharedData(); bind(); },
				engine::HookPriority::Early)) {
			state.updateInstallFailed = true;
			state.ready.store(false, std::memory_order_release);
			L->error("Shared substrate per-frame update registration failed.");
			return;
		}
		engine::RegisterPreDeferredLightsImpl(
			[] { GetSubstrateState().inDeferredLights = true; },
			engine::HookPriority::Early);
		engine::RegisterPostDeferredLightsImpl(
			[] { GetSubstrateState().inDeferredLights = false; },
			engine::HookPriority::Late);
		engine::RegisterPreDeferredLightsImpl(
			[bind] { UpdateFullscreenDebugData(GetImmediateContext(), true); bind(); },
			static_cast<engine::HookPriority>(200));
		engine::RegisterPreDeferredComposite(
			[bind] { UpdateFullscreenDebugData(GetImmediateContext(), true); bind(); },
			static_cast<engine::HookPriority>(200));
		state.updateInstalled = true;
		L->info("Shared substrate frame and producer-boundary bindings registered.");
	}

	bool IsDeferredLightsActive() noexcept
	{
		return GetSubstrateState().inDeferredLights;
	}

	void InvalidateFullscreenDebugData() noexcept
	{
		GetSubstrateState().debugFrame = UINT32_MAX;
	}

	void BindSharedData(
		ID3D11DeviceContext* a_context,
		engine::ShaderStage a_stage) noexcept
	{
		auto& state = GetSubstrateState();
		if (!a_context || !IsSharedDataReady())
			return;

		UpdateFullscreenDebugData(a_context);
		auto* debugTexture = state.debugTexture.get();
		ID3D11Buffer* buffers[kSubstrateBufferCount]{};
		for (std::size_t index = 0; index < kSubstrateBufferCount; ++index)
			buffers[index] = state.buffers[index].get();
		auto* depth = GetCanonicalSceneDepthSRV();
		switch (a_stage) {
		case engine::ShaderStage::kVertex:
			a_context->VSSetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->VSSetShaderResources(kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kPixel:
			engine::BindInjectionConstantBuffers(a_context, kFrameDataSlot, kSubstrateBufferCount, buffers);
			engine::BindInjectionShaderResources(a_context, kCanonicalDepthSlot, 1, &depth);
			engine::BindInjectionShaderResources(a_context, kFullscreenDebugTextureSlot, 1, &debugTexture);
			break;
		case engine::ShaderStage::kCompute:
			a_context->CSSetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->CSSetShaderResources(kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kCount:
			break;
		}
	}

	bool IsSharedDataCurrent() noexcept
	{
		const auto* graphicsState = engine::GetGraphicsState();
		return IsSharedDataReady() && graphicsState &&
		       GetSubstrateState().lastFrame.load(std::memory_order_relaxed) == graphicsState->frameCount;
	}
}
