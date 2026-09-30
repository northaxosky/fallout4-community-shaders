#include "Render/SharedData.h"

#include "FeatureBuffer.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/RenderHooks.h"
#include "Render/SharedDataLayout.h"
#include "Render/TemporalRenderer.h"
#include "Utils/CSBuffer.h"
#include "World/Sky.h"

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
			SubstrateBindingSnapshot savedPixelBindings;
			SubstrateBindingSnapshot savedVertexBindings;
			DirectX::XMFLOAT2 previousRatio{ 1.0f, 1.0f };
			bool hasResolutionHistory = false;
			// Render thread only.
			float timer = 0.0f;
			bool updateInstalled = false;
			bool updateInstallFailed = false;
			bool inDeferredLights = false;
			std::uint32_t pixelBindingDepth = 0;
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
			auto& fo4 = a_data.fo4;
			fo4.screenSpaceShadowsSettings = a_features.screenSpaceShadowsSettings;
			fo4.screenSpaceGISettings = a_features.screenSpaceGISettings;
			fo4.inverseSquareLightingSettings = a_features.inverseSquareLightingSettings;
			fo4.waterEffectsSettings = a_features.waterEffectsSettings;
			fo4.exponentialHeightFogSettings = a_features.exponentialHeightFogSettings;
			fo4.WetnessDebugVisualization = a_features.wetnessEffectsSettings.DebugVisualization;
			fo4.TerrainShadowMode = a_features.terrainShadowsSettings.TerrainShadowMode;
			fo4.HeightRange = DirectX::XMFLOAT2(a_features.terrainShadowsSettings.HeightRange);
			fo4.DebugHeightRange = DirectX::XMFLOAT2(a_features.terrainShadowsSettings.DebugHeightRange);
			fo4.DynamicCubemapsDebugVisualization = a_features.dynamicCubemapsSettings.DebugVisualization;
			fo4.EnabledSSR = a_features.dynamicCubemapsSettings.EnabledSSR;
			a_data.feature.cubemapCreatorSettings.Enabled = a_features.dynamicCubemapsSettings.Enabled;
			auto& terrain = a_data.feature.terraOccSettings;
			terrain.EnableTerrainShadow = fo4.TerrainShadowMode != 0;
			std::ranges::copy(a_features.terrainShadowsSettings.Scale, terrain.Scale);
			std::ranges::copy(a_features.terrainShadowsSettings.ZRange, terrain.ZRange);
			std::ranges::copy(a_features.terrainShadowsSettings.Offset, terrain.Offset);
			auto& wetness = a_data.feature.wetnessEffectsSettings;
			const auto& source = a_features.wetnessEffectsSettings;
			if (source.Active) {
				wetness.EnableWetnessEffects = source.EnableWetnessEffects;
				wetness.Wetness = source.Wetness;
				wetness.PuddleWetness = source.PuddleWetness;
				wetness.MaxRainWetness = source.MaxRainWetness;
				wetness.MaxPuddleWetness = source.MaxPuddleWetness;
				wetness.MaxShoreWetness = source.MaxShoreWetness;
				wetness.ShoreRange = source.ShoreRange;
				wetness.PuddleRadius = source.PuddleRadius;
				wetness.PuddleMaxAngle = source.PuddleMaxAngle;
				wetness.MinRainWetness = source.MinRainWetness;
			}
		}

		constexpr std::array<std::size_t, kSubstrateBufferCount> kBufferSizes{
			sizeof(FrameDataCB), sizeof(SharedDataCB), sizeof(SharedFeatureDataCB), sizeof(FO4SharedDataCB)
		};

		bool WriteSubstrate(ID3D11DeviceContext* a_context, const SubstrateData& a_data) noexcept;

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

		void SavePixelBindings() noexcept
		{
			auto& state = GetSubstrateState();
			if (state.pixelBindingDepth != 0) {
				++state.pixelBindingDepth;
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Shared substrate pixel-binding scopes overlap; preserving the active snapshot.");
				return;
			}
			auto* context = GetImmediateContext();
			if (!context || !IsSharedDataReady())
				return;

			state.savedPixelBindings.Save(context, engine::ShaderStage::kPixel);
			state.savedVertexBindings.Save(context, engine::ShaderStage::kVertex);
			state.pixelBindingDepth = 1;
		}

		void RestorePixelBindings() noexcept
		{
			auto& state = GetSubstrateState();
			if (state.pixelBindingDepth == 0)
				return;
			if (state.pixelBindingDepth > 1) {
				--state.pixelBindingDepth;
				return;
			}

			if (auto* context = GetImmediateContext()) {
				state.savedPixelBindings.Restore(context, engine::ShaderStage::kPixel);
				state.savedVertexBindings.Restore(context, engine::ShaderStage::kVertex);
			}
			state.pixelBindingDepth = 0;
		}

		void SaveDeferredLightBindings() noexcept
		{
			GetSubstrateState().inDeferredLights = true;
			SavePixelBindings();
		}

		void RestoreDeferredLightBindings() noexcept
		{
			RestorePixelBindings();
			GetSubstrateState().inDeferredLights = false;
		}

		void UpdateSharedData() noexcept
		{
			auto& state = GetSubstrateState();
			if (!state.ready.load(std::memory_order_acquire))
				return;

			auto* graphicsState = engine::GetGraphicsState();
			auto* rendererData = RE::BSGraphics::GetRendererData();
			auto* context = rendererData ? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) : nullptr;
			const auto camera = engine::GetWorldCameraRecord();
			if (!graphicsState || !context || !camera)
				return;

			const auto frame = graphicsState->frameCount;
			if (state.lastFrame.load(std::memory_order_relaxed) == frame)
				return;

			try {
				const auto delta = GetRealTimeDelta();
				const auto nextTimer = state.timer + delta;
				SubstrateData data{};
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
				PackFeatures(data, GetFeatureBufferData());
				data.fo4.DeltaTime = delta;
				UpdateCanonicalDepth(context, *camera);
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

		bool WriteSubstrate(ID3D11DeviceContext* a_context, const SubstrateData& a_data) noexcept
		{
			const void* sources[]{ &a_data.frame, &a_data.shared, &a_data.feature, &a_data.fo4 };
			auto& state = GetSubstrateState();
			for (std::size_t index = 0; index < kSubstrateBufferCount; ++index)
				if (!WriteConstantBuffer(a_context, state.buffers[index].get(), sources[index], kBufferSizes[index]))
					return false;
			return true;
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
		if (!engine::RegisterPostDeferredPrePass(
				[] { UpdateSharedData(); },
				engine::HookPriority::Early)) {
			state.updateInstallFailed = true;
			state.ready.store(false, std::memory_order_release);
			L->error("Shared substrate per-frame update registration failed.");
			return;
		}
		engine::RegisterPreDeferredLightsImpl(
			[] { SaveDeferredLightBindings(); },
			engine::HookPriority::Early);
		engine::RegisterPostDeferredLightsImpl(
			[] { RestoreDeferredLightBindings(); },
			engine::HookPriority::Late);
		// FO4 shadow-caches state; it won't reissue clobbered bindings.
		const bool compositeScopeInstalled =
			engine::RegisterPreDeferredComposite(
				[] { SavePixelBindings(); },
				engine::HookPriority::Early) &&
			engine::RegisterPostDeferredComposite(
				[] { RestorePixelBindings(); },
				engine::HookPriority::Late);
		if (!compositeScopeInstalled) {
			state.updateInstallFailed = true;
			state.ready.store(false, std::memory_order_release);
			L->error("Shared substrate composite binding scope registration failed.");
			return;
		}
		state.updateInstalled = true;
		L->info("Shared substrate update and deferred binding scopes registered.");
	}

	bool IsDeferredLightsActive() noexcept
	{
		return GetSubstrateState().inDeferredLights;
	}

	void BindSharedData(
		ID3D11DeviceContext* a_context,
		engine::ShaderStage a_stage) noexcept
	{
		auto& state = GetSubstrateState();
		if (!a_context || !IsSharedDataReady())
			return;

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
			a_context->PSSetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->PSSetShaderResources(kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kCompute:
			a_context->CSSetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->CSSetShaderResources(kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kCount:
			break;
		}
	}
}
