#include "Render/SharedData.h"

#include "FeatureBuffer.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/RenderHooks.h"
#include "Utils/CSBuffer.h"
#include "World/Sky.h"

#include <DirectXMath.h>
#include <array>
#include <atomic>
#include <cstring>
#include <d3d11.h>
#include <exception>
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
			DirectX::XMFLOAT4 WorldUpView{};
			DirectX::XMFLOAT4 ViewToWorld[3]{};
			DirectX::XMFLOAT4 CameraPositionWS{};
		};
		static_assert(sizeof(SharedDataCB) == 192);
		STATIC_ASSERT_ALIGNAS_16(SharedDataCB);

		struct SubstrateState
		{
			winrt::com_ptr<ID3D11Buffer> sharedDataCB;
			winrt::com_ptr<ID3D11Buffer> featureDataCB;
			std::atomic_bool             ready{ false };
			std::atomic_uint32_t         lastFrame{ UINT32_MAX };
			std::array<std::atomic<float>, 4> publishedWorldUpView{};
			std::array<winrt::com_ptr<ID3D11Buffer>, 2>
				savedPixelBuffers;
			// Render thread only.
			float                        timer = 0.0f;
			bool                         updateInstalled = false;
			bool                         updateInstallFailed = false;
			std::uint32_t                pixelBindingDepth = 0;
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

			if (auto* graphicsState = engine::GetGraphicsState()) {
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
				// third column of the camera rotation, absorbing the Ni-to-D3D swizzle
				const auto& rotate = a_sceneCamera->world.rotate;
				data.WorldUpView = {
					rotate.entry[2].z,
					rotate.entry[1].z,
					rotate.entry[0].z,
					1.0f
				};
				// Match the established Ni view-to-world convention.
				data.ViewToWorld[0] = {
					rotate.entry[0].x,
					rotate.entry[0].y,
					rotate.entry[0].z,
					0.0f
				};
				data.ViewToWorld[1] = {
					rotate.entry[1].x,
					rotate.entry[1].y,
					rotate.entry[1].z,
					0.0f
				};
				data.ViewToWorld[2] = {
					rotate.entry[2].x,
					rotate.entry[2].y,
					rotate.entry[2].z,
					0.0f
				};
				const auto& translate = a_sceneCamera->world.translate;
				data.CameraPositionWS = {
					translate.x,
					translate.y,
					translate.z,
					1.0f
				};
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
			ID3D11Buffer* buffers[2]{};
			context->PSGetConstantBuffers(kSharedDataSlot, 2, buffers);
			for (std::size_t index = 0; index < state.savedPixelBuffers.size(); ++index)
				state.savedPixelBuffers[index].attach(buffers[index]);
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
				ID3D11Buffer* buffers[2] = {
					state.savedPixelBuffers[0].get(),
					state.savedPixelBuffers[1].get()
				};
				context->PSSetConstantBuffers(kSharedDataSlot, 2, buffers);
			}
			for (auto& buffer : state.savedPixelBuffers)
				buffer = nullptr;
			state.pixelBindingDepth = 0;
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
				// published for drift checks against the native view-to-world row 2
				state.publishedWorldUpView[0].store(
					sharedData.WorldUpView.x, std::memory_order_relaxed);
				state.publishedWorldUpView[1].store(
					sharedData.WorldUpView.y, std::memory_order_relaxed);
				state.publishedWorldUpView[2].store(
					sharedData.WorldUpView.z, std::memory_order_relaxed);
				state.publishedWorldUpView[3].store(
					sharedData.WorldUpView.w, std::memory_order_relaxed);
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
		DX::ThrowIfFailed(
			a_device->CreateBuffer(&sharedDesc, nullptr, state.sharedDataCB.put()));
		DX::ThrowIfFailed(
			a_device->CreateBuffer(&featureDesc, nullptr, state.featureDataCB.put()));

		// engine state is unavailable during D3D bootstrap
		const SharedDataCB sharedData{};
		const FeatureDataCB featureData{};
		if (!WriteConstantBuffer(
				a_context,
				state.sharedDataCB.get(),
				&sharedData,
				sizeof(sharedData))
			|| !WriteConstantBuffer(
				a_context,
				state.featureDataCB.get(),
				&featureData,
				sizeof(featureData))) {
			state.sharedDataCB = nullptr;
			state.featureDataCB = nullptr;
			throw std::runtime_error("Shared substrate constant-buffer seeding failed.");
		}
		state.ready.store(true, std::memory_order_release);
		L->info(
			"Shared substrate ready: b{} shared_data={} bytes, b{} feature_data={} bytes.",
			kSharedDataSlot,
			sizeof(SharedDataCB),
			kFeatureDataSlot,
			sizeof(FeatureDataCB));
	}

	bool IsSharedDataReady() noexcept
	{
		const auto& state = GetSubstrateState();
		return state.updateInstalled
			&& !state.updateInstallFailed
			&& state.ready.load(std::memory_order_acquire);
	}

	std::array<float, 4> GetPublishedWorldUpView() noexcept
	{
		const auto& state = GetSubstrateState();
		return {
			state.publishedWorldUpView[0].load(std::memory_order_relaxed),
			state.publishedWorldUpView[1].load(std::memory_order_relaxed),
			state.publishedWorldUpView[2].load(std::memory_order_relaxed),
			state.publishedWorldUpView[3].load(std::memory_order_relaxed)
		};
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
			[] { SavePixelBindings(); },
			engine::HookPriority::Early);
		engine::RegisterPostDeferredLightsImpl(
			[] { RestorePixelBindings(); },
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

	void BindSharedData(ID3D11DeviceContext* a_context) noexcept
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
		a_context->PSSetConstantBuffers(kSharedDataSlot, 2, buffers);
	}
}
