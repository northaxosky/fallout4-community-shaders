#include "Render/SharedData.h"

#include "Feature.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/ForwardDraw.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedDataLayout.h"
#include "Render/TemporalRenderer.h"
#include "Utils/CSBuffer.h"
#include "Utils/SphericalHarmonics.h"
#include "World/Sky.h"
#include "World/Water.h"

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <exception>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <winrt/base.h>

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shareddata");
		std::atomic_bool g_historyResetRequested{ false };
		std::atomic_uint64_t g_historyResetFrame{ UINT64_MAX };
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
			winrt::com_ptr<ID3D11SamplerState> linearClamp;
			winrt::com_ptr<ID3D11SamplerState> pointClamp;
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

		// Upstream projects linearized DALC axes; FO4 DALC decodes with power 2.2.
		void PackLinearAmbient(const DirectX::XMFLOAT4 (&a_rows)[3], SharedDataCB& a_data)
		{
			float3 axes[6]{};
			for (std::size_t channel = 0; channel < 3; ++channel) {
				const auto& row = a_rows[channel];
				const float terms[3]{ row.x, row.y, row.z };
				for (std::size_t axis = 0; axis < 6; ++axis) {
					const float term = axis % 2 == 0 ? terms[axis / 2] : -terms[axis / 2];
					(&axes[axis].x)[channel] = std::pow((std::max)(row.w + term, 0.0f), 2.2f);
				}
			}
			const auto sh = SphericalHarmonics::DALCToSH(axes);
			const auto pack = [](const SphericalHarmonics::SH2& a_sh) {
				return DirectX::XMFLOAT4{ a_sh.c0, a_sh.c1[0], a_sh.c1[1], a_sh.c1[2] };
			};
			a_data.AmbientSHR = pack(sh.r);
			a_data.AmbientSHG = pack(sh.g);
			a_data.AmbientSHB = pack(sh.b);
		}

		SharedDataCB BuildSharedData(
			float a_timer,
			const engine::WorldCameraRecord& a_camera,
			FO4SharedDataCB& a_fo4)
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
				{ a_camera.PosAdjust.x, a_camera.PosAdjust.y, a_camera.PosAdjust.z });

			float sunX = 0.0f;
			float sunY = 0.0f;
			float sunZ = 0.0f;
			if (engine::TryGetSunDirectionWS(sunX, sunY, sunZ)) {
				data.DirLightDirection = { -sunX, -sunY, -sunZ, 0.0f };
				data.SunDirection = data.DirLightDirection;
			}
			// FO4: clouds follow the drawn disc, not the clamped light.
			a_fo4.SunDiscDirection = data.DirLightDirection;
			if (float discX, discY, discZ; engine::TryGetSunDiscDirectionWS(discX, discY, discZ))
				a_fo4.SunDiscDirection = { discX, discY, discZ, 0.0f };

			DirectX::XMFLOAT3 sunColor{};
			if (engine::TryGetSunLightColor(sunColor))
				data.DirLightColor = { sunColor.x, sunColor.y, sunColor.z, 1.0f };
			if (engine::TryGetDirectionalAmbientRows(a_fo4.DirectionalAmbient))
				PackLinearAmbient(a_fo4.DirectionalAmbient, data);
			data.HideSky = engine::IsSkyHidden() ? 1u : 0u;

			data.InInterior = engine::IsInterior() ? 1u : 0u;
			// FO4: Interior Sun is not ported, so only exteriors have sun shadows.
			data.HasDirectionalShadows = data.InInterior ? 0u : 1u;

			return data;
		}

		void PackFeatures(SubstrateData& a_data)
		{
			const auto& state = GetSubstrateState();
			a_data.feature = {};
			// FO4 colors are linear: identity diffuse decode, no pi on sun radiance.
			a_data.feature.linearLightingSettings = {
				.isMainOrLoadingMenu = 0,
				.diffuseGamma = 1.0f,
				.directionalLightMult = 1.0f,
				// Upstream: legacy point-light colors are a white Lambert response.
				.pointLightMult = std::numbers::pi_v<float>,
				.ambientMult = 1.0f,
				.glowmapMult = 1.0f,
				.effectLightingMult = 1.0f,
				.membraneEffectMult = 1.0f,
				.bloodEffectMult = 1.0f,
				.projectedEffectMult = 1.0f,
				.deferredEffectMult = 1.0f,
				.otherEffectMult = 1.0f,
				.particleEffectMult = 1.0f,
				.lightSpriteEffectMult = 1.0f,
				.fireEffectMult = 1.0f,
				.fireEffectCurve = 1.0f,
				.diffuseCurve = 1.0f,
				.diffuseWhiteReflectance = 1.0f
			};
			a_data.fo4 = {
				.DebugOwner = state.fo4.DebugOwner,
				.DebugMode = state.fo4.DebugMode,
				.DeltaTime = state.fo4.DeltaTime,
				.DebugParams = state.fo4.DebugParams,
				.SunDiscDirection = state.fo4.SunDiscDirection
			};
			std::ranges::copy(state.fo4.DirectionalAmbient, a_data.fo4.DirectionalAmbient);
			FeatureManager::Get().WriteSharedDataAll(a_data.feature, a_data.fo4);
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
				PackFeatures(data);
				if (current) {
					// Terrain prepares after material draws; refresh b6/b7, not camera history.
					if (!WriteSubstrate(context, data, kFeatureDataSlot - kFrameDataSlot))
						CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Shared feature constant-buffer map failed.");
					return;
				}
				const auto delta = GetRealTimeDelta();
				const auto nextTimer = state.timer + delta;
				data.shared = BuildSharedData(nextTimer, *camera, data.fo4);
				const auto* manager = engine::GetRenderTargetManager();
				// OG deferred, water and tiled-lighting shaders carry no dynamic-resolution constants.
				const bool fullScale = REX::FModule::IsRuntimeOG();
				const DirectX::XMFLOAT2 ratio{
					manager && !fullScale ? manager->GetDynamicWidthRatio() : 1.0f,
					manager && !fullScale ? manager->GetDynamicHeightRatio() : 1.0f
				};
				if (!(ratio.x > 0.0f) || !(ratio.y > 0.0f))
					return;
				const auto previousRatio = state.hasResolutionHistory ? state.previousRatio : ratio;
				const auto* clampTarget = engine::ResolveRenderTarget(RE::BSGraphics::RenderTargetID::kMain);
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
				data.shared.ResetHistory = IsHistoryResetFrame() ? 1u : 0u;
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
			if (state.debugSelectionFrame != graphics->frameCount) {
				state.debugFeature = FeatureManager::Get().GetFullscreenDebugFeature();
				state.debugSelectionFrame = graphics->frameCount;
			}
			if (state.debugFeature && !state.debugFeature->IsHealthy()) {
				state.debugFeature = nullptr;
				a_refresh = true;
			}
			if (state.debugFrame == graphics->frameCount && !a_refresh)
				return;
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
		forward_draw::Initialize(a_device);

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(a_device->CreateSamplerState(&samplerDesc, state.linearClamp.put()));
		annotation::SetName(state.linearClamp.get(), "Render/LinearClamp.Sampler");
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(a_device->CreateSamplerState(&samplerDesc, state.pointClamp.put()));
		annotation::SetName(state.pointClamp.get(), "Render/PointClamp.Sampler");

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

	ID3D11SamplerState* GetLinearClampSampler() noexcept
	{
		return GetSubstrateState().linearClamp.get();
	}

	ID3D11SamplerState* GetPointClampSampler() noexcept
	{
		return GetSubstrateState().pointClamp.get();
	}

	void RequestHistoryReset() noexcept
	{
		g_historyResetRequested.store(true, std::memory_order_release);
	}

	void LatchHistoryResetFrame() noexcept
	{
		const auto* graphics = engine::GetGraphicsState();
		if (graphics && !engine::IsMainOrLoadingMenuOpen() &&
			g_historyResetRequested.exchange(false, std::memory_order_acq_rel))
			g_historyResetFrame.store(graphics->frameCount, std::memory_order_release);
	}

	bool IsHistoryResetFrame() noexcept
	{
		const auto* graphics = engine::GetGraphicsState();
		return graphics && g_historyResetFrame.load(std::memory_order_acquire) == graphics->frameCount;
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
		// Upstream Renderer_ResetState rebind; FO4's Begin clears b4-b7 first.
		if (!engine::RegisterPostResetState(bind) ||
			!engine::RegisterPreDeferredPrePass(
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
			engine::BindFrameConstantBuffers(a_context, a_stage, kFrameDataSlot, kSubstrateBufferCount, buffers);
			engine::BindFrameShaderResources(a_context, a_stage, kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kPixel:
			engine::BindFrameConstantBuffers(a_context, a_stage, kFrameDataSlot, kSubstrateBufferCount, buffers);
			engine::BindFrameShaderResources(a_context, a_stage, kCanonicalDepthSlot, 1, &depth);
			engine::BindFrameShaderResources(a_context, a_stage, kFullscreenDebugTextureSlot, 1, &debugTexture);
			break;
		case engine::ShaderStage::kCompute:
			engine::BindFrameConstantBuffers(a_context, a_stage, kFrameDataSlot, kSubstrateBufferCount, buffers);
			engine::BindFrameShaderResources(a_context, a_stage, kCanonicalDepthSlot, 1, &depth);
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
