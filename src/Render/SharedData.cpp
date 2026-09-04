#include "Render/SharedData.h"

#include "FeatureBuffer.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/RenderHooks.h"
#include "Utils/CSBuffer.h"
#include "World/Sky.h"

#include <DirectXMath.h>
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
		// mirrors HLSL SharedData at b5
		struct alignas(16) SharedDataCB
		{
			DirectX::XMFLOAT4 CameraData{};
			DirectX::XMFLOAT4 BufferDim{};
			DirectX::XMFLOAT4 DynamicResolution{};
			DirectX::XMFLOAT4 NDCToViewMul{};
			DirectX::XMFLOAT4 NDCToViewAdd{};
			DirectX::XMFLOAT4 SunDirection{};
			float             Timer = 0.0f;
			float             DeltaTime = 0.0f;
			std::uint32_t     FrameCount = 0;
			std::uint32_t     InInterior = 0;
		};
		static_assert(sizeof(SharedDataCB) == 112);
		STATIC_ASSERT_ALIGNAS_16(SharedDataCB);

		struct SubstrateState
		{
			winrt::com_ptr<ID3D11Buffer> sharedDataCB;
			winrt::com_ptr<ID3D11Buffer> featureDataCB;
			winrt::com_ptr<ID3D11Buffer> skylightingDataCB;
			std::atomic_bool             ready{ false };
			std::atomic_uint32_t         lastFrame{ UINT32_MAX };
			std::array<winrt::com_ptr<ID3D11Buffer>, 3>
				savedPixelBuffers;
			std::array<winrt::com_ptr<ID3D11Buffer>, 2>
				savedComputeBuffers;
			winrt::com_ptr<ID3D11ShaderResourceView> savedSkylightingSRV;
			winrt::com_ptr<ID3D11SamplerState> savedSkylightingSampler;
			std::mutex                   skylightingMutex;
			SkylightingSharedData        skylightingData{};
			ID3D11ShaderResourceView*    skylightingSRV = nullptr;
			ID3D11SamplerState*          skylightingSampler = nullptr;
			std::uint64_t                skylightingGeneration = 0;
			std::uint64_t                skylightingWrittenGeneration = 0;
			std::uint64_t                skylightingWrittenCameraSequence = 0;
			std::atomic_bool             skylightingDataPublished{ false };
			std::atomic_bool             skylightingBoundLastCall{ false };
			std::atomic_bool             skylightingCameraPublishedLastCall{ false };
			std::atomic_uint64_t         skylightingPublishCalls{ 0 };
			std::atomic_uint64_t         skylightingBufferWrites{ 0 };
			std::atomic_uint64_t         skylightingBindCalls{ 0 };
			std::atomic_uint64_t         skylightingSuccessfulBinds{ 0 };
			std::atomic_uint64_t         skylightingRejectedNoBuffer{ 0 };
			std::atomic_uint64_t         skylightingRejectedNoData{ 0 };
			std::atomic_uint64_t         skylightingRejectedNoSRV{ 0 };
			std::atomic_uint64_t         skylightingRejectedNoSampler{ 0 };
			std::atomic_uint64_t         skylightingRejectedCameraMissing{ 0 };
			std::atomic_uint64_t         skylightingRejectedCameraStale{ 0 };
			// Render thread only.
			float                        timer = 0.0f;
			bool                         updateInstalled = false;
			bool                         updateInstallFailed = false;
			bool                         inDeferredLights = false;
			std::uint32_t                pixelBindingDepth = 0;
			std::uint32_t                computeBindingDepth = 0;
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
			float a_deltaTime,
			float a_timer,
			const RE::NiCamera* a_sceneCamera)
		{
			SharedDataCB data{};
			data.DeltaTime = a_deltaTime;
			data.Timer = a_timer;

			auto* graphicsState = engine::GetGraphicsState();
			if (graphicsState) {
				const auto width = static_cast<float>(graphicsState->screenWidth);
				const auto height = static_cast<float>(graphicsState->screenHeight);
				data.BufferDim = { width, height, Reciprocal(width), Reciprocal(height) };
				data.FrameCount = graphicsState->frameCount;
			}

			if (auto* renderTargetManager = engine::GetRenderTargetManager()) {
				const auto widthRatio = renderTargetManager->GetDynamicWidthRatio();
				const auto heightRatio = renderTargetManager->GetDynamicHeightRatio();
				data.DynamicResolution = {
					widthRatio,
					heightRatio,
					Reciprocal(widthRatio),
					Reciprocal(heightRatio)
				};
			}

			if (a_sceneCamera) {
				DirectX::XMFLOAT4X4 projection;
				DirectX::XMFLOAT4X4 inverseProjection;
				DirectX::XMFLOAT4   ndcToViewMul;
				DirectX::XMFLOAT4   ndcToViewAdd;
				// structured binding avoids legacy near/far macros
				const auto& [left, right, top, bottom, nearZ, farZ, ortho] =
					a_sceneCamera->viewFrustum;
				if (RE::BuildPerspectiveFromFrustum(
						a_sceneCamera->viewFrustum,
						projection,
						inverseProjection,
						ndcToViewMul,
						ndcToViewAdd)) {
					data.CameraData = { farZ, nearZ, farZ - nearZ, farZ * nearZ };
					data.NDCToViewMul = ndcToViewMul;
					data.NDCToViewAdd = ndcToViewAdd;
				}
			}

			float sunX = 0.0f;
			float sunY = 0.0f;
			float sunZ = 0.0f;
			if (engine::TryGetSunDirectionWS(sunX, sunY, sunZ))
				data.SunDirection = { sunX, sunY, sunZ, 1.0f };

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (const auto* cell = player ? player->GetParentCell() : nullptr)
				data.InInterior = cell->IsExterior() ? 0u : 1u;

			return data;
		}

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

		void RefreshAndBindSkylightingData(
			ID3D11DeviceContext* a_context) noexcept
		{
			auto& state = GetSubstrateState();
			state.skylightingBindCalls.fetch_add(1, std::memory_order_relaxed);
			state.skylightingBoundLastCall.store(false, std::memory_order_relaxed);
			state.skylightingCameraPublishedLastCall.store(
				false, std::memory_order_relaxed);

			if (!a_context || !state.skylightingDataCB) {
				state.skylightingRejectedNoBuffer.fetch_add(
					1, std::memory_order_relaxed);
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Skylighting shared-data bind was called before b{} existed.",
					kSkylightingDataSlot);
				return;
			}

			SkylightingSharedData data{};
			ID3D11ShaderResourceView* srv = nullptr;
			ID3D11SamplerState* sampler = nullptr;
			std::uint64_t generation = 0;
			{
				const std::lock_guard lock(state.skylightingMutex);
				if (!state.skylightingDataPublished.load(
						std::memory_order_relaxed)) {
					state.skylightingRejectedNoData.fetch_add(
						1, std::memory_order_relaxed);
					return;
				}
				data = state.skylightingData;
				srv = state.skylightingSRV;
				sampler = state.skylightingSampler;
				generation = state.skylightingGeneration;
			}

			bool resourcesAvailable = true;
			if (!srv) {
				state.skylightingRejectedNoSRV.fetch_add(
					1, std::memory_order_relaxed);
				CS_LOG_ONCE(
					L,
					spdlog::level::warn,
					"Skylighting consumer bind was called without the t{} "
					"occlusion SRV; taking the identity path.",
					kSkylightingTextureSlot);
				resourcesAvailable = false;
			}
			if (!sampler) {
				state.skylightingRejectedNoSampler.fetch_add(
					1, std::memory_order_relaxed);
				CS_LOG_ONCE(
					L,
					spdlog::level::warn,
					"Skylighting consumer bind was called without the s{} "
					"comparison sampler; taking the identity path.",
					kSkylightingSamplerSlot);
				resourcesAvailable = false;
			}

			const auto& camera = engine::GetFrameBuffer();
			auto* graphicsState = engine::GetGraphicsState();
			const auto currentFrame =
				graphicsState ? graphicsState->frameCount : UINT32_MAX;
			const bool cameraCurrent =
				camera.valid && camera.frameCount == currentFrame;
			if (data.Mode != 0 && !camera.valid) {
				state.skylightingRejectedCameraMissing.fetch_add(
					1, std::memory_order_relaxed);
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Skylighting consumer was called without an exact "
					"fullscreen-light camera snapshot; taking the identity path.");
			} else if (data.Mode != 0 && !cameraCurrent) {
				state.skylightingRejectedCameraStale.fetch_add(
					1, std::memory_order_relaxed);
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Skylighting consumer received a stale fullscreen-light "
					"camera snapshot; taking the identity path.");
			}

			if (resourcesAvailable && cameraCurrent) {
				for (std::size_t row = 0; row < 3; ++row)
					data.ViewToWorld[row] = camera.data.ViewToWorld[row];
				data.CameraPosAdjust = camera.data.CameraPosAdjust;
				state.skylightingCameraPublishedLastCall.store(
					true, std::memory_order_relaxed);
			} else {
				data.Mode = 0;
			}

			const auto cameraSequence = cameraCurrent ? camera.sequence : 0;
			bool needsWrite = false;
			{
				const std::lock_guard lock(state.skylightingMutex);
				needsWrite =
					state.skylightingWrittenGeneration != generation
					|| state.skylightingWrittenCameraSequence != cameraSequence;
			}
			if (needsWrite) {
				if (!WriteConstantBuffer(
						a_context,
						state.skylightingDataCB.get(),
						&data,
						sizeof(data))) {
					state.skylightingRejectedNoBuffer.fetch_add(
						1, std::memory_order_relaxed);
					CS_LOG_ONCE(
						L,
						spdlog::level::err,
						"Skylighting shared-data b{} map failed.",
						kSkylightingDataSlot);
					return;
				}
				{
					const std::lock_guard lock(state.skylightingMutex);
					state.skylightingWrittenGeneration = generation;
					state.skylightingWrittenCameraSequence = cameraSequence;
				}
				state.skylightingBufferWrites.fetch_add(
					1, std::memory_order_relaxed);
			}

			ID3D11Buffer* buffer = state.skylightingDataCB.get();
			a_context->PSSetConstantBuffers(
				kSkylightingDataSlot, 1, &buffer);
			if (resourcesAvailable && cameraCurrent) {
				a_context->PSSetShaderResources(
					kSkylightingTextureSlot, 1, &srv);
				a_context->PSSetSamplers(
					kSkylightingSamplerSlot, 1, &sampler);
			}
			const bool bound = resourcesAvailable && cameraCurrent;
			state.skylightingBoundLastCall.store(
				bound, std::memory_order_relaxed);
			if (bound) {
				state.skylightingSuccessfulBinds.fetch_add(
					1, std::memory_order_relaxed);
				CS_LOG_ONCE(
					L,
					spdlog::level::info,
					"Skylighting consumer first accepted bind: b{} t{} s{} "
					"camera_sequence={} frame={}.",
					kSkylightingDataSlot,
					kSkylightingTextureSlot,
					kSkylightingSamplerSlot,
					cameraSequence,
					currentFrame);
			}
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

			for (auto& buffer : state.savedPixelBuffers)
				buffer = nullptr;
			ID3D11Buffer* buffers[3]{};
			context->PSGetConstantBuffers(kSharedDataSlot, 3, buffers);
			for (std::size_t index = 0; index < state.savedPixelBuffers.size(); ++index)
				state.savedPixelBuffers[index].attach(buffers[index]);
			ID3D11ShaderResourceView* srv = nullptr;
			context->PSGetShaderResources(kSkylightingTextureSlot, 1, &srv);
			state.savedSkylightingSRV.attach(srv);
			ID3D11SamplerState* sampler = nullptr;
			context->PSGetSamplers(kSkylightingSamplerSlot, 1, &sampler);
			state.savedSkylightingSampler.attach(sampler);
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
				ID3D11Buffer* buffers[3] = {
					state.savedPixelBuffers[0].get(),
					state.savedPixelBuffers[1].get(),
					state.savedPixelBuffers[2].get()
				};
				context->PSSetConstantBuffers(kSharedDataSlot, 3, buffers);
				ID3D11ShaderResourceView* srv =
					state.savedSkylightingSRV.get();
				context->PSSetShaderResources(
					kSkylightingTextureSlot, 1, &srv);
				ID3D11SamplerState* sampler =
					state.savedSkylightingSampler.get();
				context->PSSetSamplers(
					kSkylightingSamplerSlot, 1, &sampler);
			}
			for (auto& buffer : state.savedPixelBuffers)
				buffer = nullptr;
			state.savedSkylightingSRV = nullptr;
			state.savedSkylightingSampler = nullptr;
			state.pixelBindingDepth = 0;
		}

		void SaveComputeBindings() noexcept
		{
			auto& state = GetSubstrateState();
			if (state.computeBindingDepth != 0) {
				++state.computeBindingDepth;
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Shared substrate compute-binding scopes overlap; preserving the active snapshot.");
				return;
			}
			auto* context = GetImmediateContext();
			if (!context || !IsSharedDataReady())
				return;

			for (auto& buffer : state.savedComputeBuffers)
				buffer = nullptr;
			ID3D11Buffer* buffers[2]{};
			context->CSGetConstantBuffers(kSharedDataSlot, 2, buffers);
			for (std::size_t index = 0;
				index < state.savedComputeBuffers.size();
				++index) {
				state.savedComputeBuffers[index].attach(buffers[index]);
			}
			state.computeBindingDepth = 1;
		}

		void RestoreComputeBindings() noexcept
		{
			auto& state = GetSubstrateState();
			if (state.computeBindingDepth == 0)
				return;
			if (state.computeBindingDepth > 1) {
				--state.computeBindingDepth;
				return;
			}

			if (auto* context = GetImmediateContext()) {
				ID3D11Buffer* buffers[2] = {
					state.savedComputeBuffers[0].get(),
					state.savedComputeBuffers[1].get()
				};
				context->CSSetConstantBuffers(kSharedDataSlot, 2, buffers);
			}
			for (auto& buffer : state.savedComputeBuffers)
				buffer = nullptr;
			state.computeBindingDepth = 0;
		}

		void SaveDeferredLightBindings() noexcept
		{
			GetSubstrateState().inDeferredLights = true;
			SavePixelBindings();
			SaveComputeBindings();
		}

		void BindDeferredLightComputeData() noexcept
		{
			// Earlier feature compute scopes clear b5/b6 before tiled lighting runs.
			BindSharedData(
				GetImmediateContext(),
				engine::ShaderStage::kCompute);
		}

		void RestoreDeferredLightBindings() noexcept
		{
			RestoreComputeBindings();
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
			auto* context = rendererData
				? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context)
				: nullptr;
			auto* sceneCamera = engine::GetWorldRootCamera();
			if (!graphicsState || !context)
				return;

			const auto frame = graphicsState->frameCount;
			if (state.lastFrame.load(std::memory_order_relaxed) == frame)
				return;

			try {
				const auto delta = GetRealTimeDelta();
				const auto nextTimer = state.timer + delta;
				const auto sharedData =
					BuildSharedData(delta, nextTimer, sceneCamera);
				const auto featureData = GetFeatureBufferData();
				if (!WriteConstantBuffer(
						context,
						state.sharedDataCB.get(),
						&sharedData,
						sizeof(sharedData))
					|| !WriteConstantBuffer(
						context,
						state.featureDataCB.get(),
						&featureData,
						sizeof(featureData))) {
					CS_LOG_EVERY_MS(
						L,
						2000,
						spdlog::level::err,
						"Shared substrate constant-buffer map failed.");
					return;
				}
				state.timer = nextTimer;
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

		const auto sharedDesc = cs::buffer::ConstantBufferDesc<SharedDataCB>();
		const auto featureDesc = cs::buffer::ConstantBufferDesc<FeatureDataCB>();
		const auto skylightingDesc =
			cs::buffer::ConstantBufferDesc<SkylightingSharedData>();
		DX::ThrowIfFailed(
			a_device->CreateBuffer(&sharedDesc, nullptr, state.sharedDataCB.put()));
		DX::ThrowIfFailed(
			a_device->CreateBuffer(&featureDesc, nullptr, state.featureDataCB.put()));
		DX::ThrowIfFailed(a_device->CreateBuffer(
			&skylightingDesc, nullptr, state.skylightingDataCB.put()));
		annotation::SetName(state.sharedDataCB.get(), "Render/SharedData.Buffer");
		annotation::SetName(state.featureDataCB.get(), "Render/FeatureData.Buffer");
		annotation::SetName(
			state.skylightingDataCB.get(), "Render/SkylightingData.Buffer");

		// engine state is unavailable during D3D bootstrap
		const SharedDataCB sharedData{};
		const FeatureDataCB featureData{};
		const SkylightingSharedData skylightingData{};
		if (!WriteConstantBuffer(
				a_context,
				state.sharedDataCB.get(),
				&sharedData,
				sizeof(sharedData))
			|| !WriteConstantBuffer(
				a_context,
				state.featureDataCB.get(),
				&featureData,
				sizeof(featureData))
			|| !WriteConstantBuffer(
				a_context,
				state.skylightingDataCB.get(),
				&skylightingData,
				sizeof(skylightingData))) {
			state.sharedDataCB = nullptr;
			state.featureDataCB = nullptr;
			state.skylightingDataCB = nullptr;
			throw std::runtime_error("Shared substrate constant-buffer seeding failed.");
		}
		state.ready.store(true, std::memory_order_release);
		L->info(
			"Shared substrate ready: b{} shared_data={} bytes, b{} feature_data={} "
			"bytes, b{} skylighting_data={} bytes.",
			kSharedDataSlot,
			sizeof(SharedDataCB),
			kFeatureDataSlot,
			sizeof(FeatureDataCB),
			kSkylightingDataSlot,
			sizeof(SkylightingSharedData));
	}

	bool IsSharedDataReady() noexcept
	{
		const auto& state = GetSubstrateState();
		return state.updateInstalled
			&& !state.updateInstallFailed
			&& state.ready.load(std::memory_order_acquire);
	}

	void EnsureSharedDataUpdateInstalled()
	{
		auto& state = GetSubstrateState();
		if (state.updateInstalled || state.updateInstallFailed)
			return;
		if (!engine::RegisterPostDeferredPrePass(
			[] { UpdateSharedData(); },
			engine::HookPriority::Late)) {
			state.updateInstallFailed = true;
			state.ready.store(false, std::memory_order_release);
			L->error("Shared substrate per-frame update registration failed.");
			return;
		}
		engine::RegisterPreDeferredLightsImpl(
			[] { SaveDeferredLightBindings(); },
			engine::HookPriority::Early);
		engine::RegisterPreDeferredLightsImpl(
			[] { BindDeferredLightComputeData(); },
			engine::HookPriority::Late);
		engine::RegisterPostDeferredLightsImpl(
			[] { RestoreDeferredLightBindings(); },
			engine::HookPriority::Late);
		// FO4 shadow-caches state; it won't reissue clobbered bindings.
		const bool compositeScopeInstalled =
			engine::RegisterPreDeferredComposite(
				[] { SavePixelBindings(); },
				engine::HookPriority::Early)
			&& engine::RegisterPostDeferredComposite(
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

	void PublishSkylightingSharedData(
		const SkylightingSharedData& a_data,
		ID3D11ShaderResourceView* a_occlusionSRV,
		ID3D11SamplerState* a_comparisonSampler) noexcept
	{
		auto& state = GetSubstrateState();
		state.skylightingPublishCalls.fetch_add(1, std::memory_order_relaxed);
		{
			const std::lock_guard lock(state.skylightingMutex);
			state.skylightingData = a_data;
			state.skylightingSRV = a_occlusionSRV;
			state.skylightingSampler = a_comparisonSampler;
			++state.skylightingGeneration;
		}
		state.skylightingDataPublished.store(true, std::memory_order_release);
	}

	SkylightingSharedDataStatus GetSkylightingSharedDataStatus() noexcept
	{
		auto& state = GetSubstrateState();
		SkylightingSharedDataStatus status{
			.bufferReady = state.skylightingDataCB != nullptr,
			.dataPublished = state.skylightingDataPublished.load(
				std::memory_order_relaxed),
			.boundLastCall = state.skylightingBoundLastCall.load(
				std::memory_order_relaxed),
			.cameraPublishedLastCall =
				state.skylightingCameraPublishedLastCall.load(
					std::memory_order_relaxed),
			.publishCalls = state.skylightingPublishCalls.load(
				std::memory_order_relaxed),
			.bufferWrites = state.skylightingBufferWrites.load(
				std::memory_order_relaxed),
			.bindCalls = state.skylightingBindCalls.load(
				std::memory_order_relaxed),
			.successfulBinds = state.skylightingSuccessfulBinds.load(
				std::memory_order_relaxed),
			.rejectedNoBuffer = state.skylightingRejectedNoBuffer.load(
				std::memory_order_relaxed),
			.rejectedNoData = state.skylightingRejectedNoData.load(
				std::memory_order_relaxed),
			.rejectedNoSrv = state.skylightingRejectedNoSRV.load(
				std::memory_order_relaxed),
			.rejectedNoSampler = state.skylightingRejectedNoSampler.load(
				std::memory_order_relaxed),
			.rejectedCameraMissing =
				state.skylightingRejectedCameraMissing.load(
					std::memory_order_relaxed),
			.rejectedCameraStale =
				state.skylightingRejectedCameraStale.load(
					std::memory_order_relaxed)
		};
		{
			const std::lock_guard lock(state.skylightingMutex);
			status.srvPublished = state.skylightingSRV != nullptr;
			status.samplerPublished = state.skylightingSampler != nullptr;
		}
		return status;
	}

	void BindSharedData(
		ID3D11DeviceContext* a_context,
		engine::ShaderStage a_stage) noexcept
	{
		auto& state = GetSubstrateState();
		if (!a_context || !IsSharedDataReady())
			return;

		if (state.lastFrame.load(std::memory_order_relaxed) == UINT32_MAX) {
			UpdateSharedData();
			if (state.lastFrame.load(std::memory_order_relaxed) == UINT32_MAX) {
				const auto* graphicsState = engine::GetGraphicsState();
				const auto frame = graphicsState ? graphicsState->frameCount : UINT32_MAX;
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Shared substrate first bind has no published frame data at frame {}; binding the zero seed.",
					frame);
			}
		}

		ID3D11Buffer* buffers[2] = {
			state.sharedDataCB.get(),
			state.featureDataCB.get()
		};
		switch (a_stage) {
		case engine::ShaderStage::kVertex:
			a_context->VSSetConstantBuffers(kSharedDataSlot, 2, buffers);
			break;
		case engine::ShaderStage::kPixel:
			a_context->PSSetConstantBuffers(kSharedDataSlot, 2, buffers);
			if (state.inDeferredLights)
				RefreshAndBindSkylightingData(a_context);
			break;
		case engine::ShaderStage::kCompute:
			a_context->CSSetConstantBuffers(kSharedDataSlot, 2, buffers);
			break;
		case engine::ShaderStage::kCount:
			break;
		}
	}
}
