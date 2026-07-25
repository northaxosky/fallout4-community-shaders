#include "WetnessEffects.h"

#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/Weather.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.wetnesseffects");

		constexpr const wchar_t* kWetnessPath =
			L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\WetnessEffects\\Shaders\\WetnessMaskCS.hlsl";
		constexpr const wchar_t* kTelemetryPath =
			L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\WetnessEffects\\Shaders\\WetnessTelemetryCS.hlsl";

		bool ParseSettingsTable(
			const toml::table& a_config,
			WetnessEffects::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode) {
				return true;
			}

			const auto* settingsTable = settingsNode->as_table();
			if (!settingsTable) {
				a_error = "settings: expected table";
				return false;
			}

			switch (feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled)) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = "settings.enabled: expected boolean";
				return false;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = "settings.enabled: invalid value";
				return false;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = "settings.enabled: value is out of range";
				return false;
			}
			return false;
		}

		std::unique_ptr<cs::buffer::Texture2D> CreateTexture(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format)
		{
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = a_width;
			textureDesc.Height = a_height;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = a_format;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			auto texture = std::make_unique<cs::buffer::Texture2D>(textureDesc);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = a_format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			texture->CreateSRV(srvDesc);

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = a_format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			texture->CreateUAV(uavDesc);
			return texture;
		}

		void StoreViewToWorld(const RE::NiMatrix3& a_rotation, float* a_output)
		{
			for (std::size_t row = 0; row < 3; ++row) {
				a_output[row * 4 + 0] = a_rotation.entry[row].x;
				a_output[row * 4 + 1] = a_rotation.entry[row].y;
				a_output[row * 4 + 2] = a_rotation.entry[row].z;
				a_output[row * 4 + 3] = 0.0f;
			}
		}
	}

	WetnessEffects* WetnessEffects::GetSingleton()
	{
		static WetnessEffects instance;
		return &instance;
	}

	bool WetnessEffects::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}
		_settings = candidate;
		return true;
	}

	void WetnessEffects::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settings); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void WetnessEffects::Load()
	{
		const auto registerWetnessReplacement =
			[this](cs::engine::ShaderInjectionTarget a_target, std::uint32_t a_slot) {
				return cs::engine::RegisterReplacement({
					.targetId = a_target,
					.contributor = "WetnessEffects",
					.defines = { { "WETNESS_EFFECTS", "1" } },
					.isReady = [this] {
						return IsWetnessMaskReady();
					},
					.bind = [this, a_slot](ID3D11DeviceContext* a_context) {
						BindWetnessMask(a_context, a_slot);
					},
					.slotClaims = {
						{
							.stage = cs::engine::ShaderStage::kPixel,
							.resourceType = cs::engine::ShaderResourceType::kShaderResource,
							.slot = a_slot
						}
					}
				});
			};
		const bool directionalRegistered = registerWetnessReplacement(
			cs::engine::ShaderInjectionTarget::kBsdfLightDeferredDirectional,
			kMaskPSSlotDirectional);
		const bool directionalIblRegistered = registerWetnessReplacement(
			cs::engine::ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl,
			kMaskPSSlotDirectional);
		// Blob 3560 is the live exterior ambient reconstruction.
		constexpr bool kInjectAmbientPass = true;
		bool ambientRegistered = true;
		if constexpr (kInjectAmbientPass) {
			ambientRegistered = registerWetnessReplacement(
				cs::engine::ShaderInjectionTarget::kAmbientIblPass,
				kMaskPSSlotAmbient);
		}
		if (!directionalRegistered || !directionalIblRegistered || !ambientRegistered) {
			L->error("Failed to register wetness shader replacements.");
		}

		cs::engine::RegisterPostDeferredPrePass([] {
			WetnessEffects::GetSingleton()->OnComputeWetness();
		});
		cs::engine::RegisterPreSunLightDraw([] {
			WetnessEffects::GetSingleton()->OnPreSunLightDraw();
		});
		cs::engine::RegisterPostDeferredLightsImpl([] {
			WetnessEffects::GetSingleton()->RestoreWetnessMaskBindings();
		});
		cs::engine::RegisterPostDeferredComposite([] {
			WetnessEffects::GetSingleton()->RestoreWetnessMaskBindings();
		});
		if constexpr (kInjectAmbientPass) {
			cs::engine::RegisterPostDeferredLightsImpl([] {
				WetnessEffects::GetSingleton()->OnAmbientPassDispatch();
			});
		}
		_started.store(true, std::memory_order_release);
		L->info("Registered wetness-mask lighting callbacks (enabled={}).", _settings.enabled);
	}

	void WetnessEffects::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_started.load(std::memory_order_acquire) || !a_device) {
			return;
		}

		_wetnessCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kWetnessPath, {}, "cs_5_0")));
		_telemetryCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kTelemetryPath, {}, "cs_5_0")));
		_wetnessCB = std::make_unique<cs::buffer::ConstantBuffer>(
			cs::buffer::ConstantBufferDesc<WetnessCB>());
		_telemetryCB = std::make_unique<cs::buffer::ConstantBuffer>(
			cs::buffer::ConstantBufferDesc<TelemetryCB>());

		if (!_wetnessCS) {
			L->warn("Failed to compile wetness-mask shader.");
		}
		if (!_telemetryCS) {
			L->warn("Failed to compile wetness telemetry shader.");
		}
		// Allocate regardless of the runtime toggle: injection readiness is frozen
		// once at startup, so gating here permanently drops the shader replacement.
		EnsureResources();
	}

	bool WetnessEffects::IsWetnessMaskReady() const
	{
		return _started.load(std::memory_order_acquire) &&
			_wetnessCS &&
			_wetnessCB &&
			_resourcesReady.load(std::memory_order_acquire);
	}

	bool WetnessEffects::EnsureResources()
	{
		if (_resourceInitFailed.load(std::memory_order_acquire) || !cs::util::GetD3DDevice()) {
			return false;
		}

		const bool hadResources = _resourcesReady.load(std::memory_order_acquire);
		try {
			auto* state = cs::engine::GetGraphicsState();
			if (!state || state->screenWidth == 0 || state->screenHeight == 0) {
				if (hadResources) {
					return true;
				}
				throw std::runtime_error("graphics state has no screen dimensions");
			}

			const std::uint32_t width = state->screenWidth;
			const std::uint32_t height = state->screenHeight;
			if (hadResources && width == _allocWidth && height == _allocHeight) {
				return true;
			}

			auto* rendererData = RE::BSGraphics::GetRendererData();
			auto* context = rendererData ?
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
				nullptr;
			if (!context) {
				throw std::runtime_error("renderer context unavailable");
			}

			auto wetnessMask = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
			const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			context->ClearUnorderedAccessViewFloat(wetnessMask->uav.get(), zero);
			auto telemetryStats =
				CreateTexture(kTelemetryWidth, kTelemetryHeight, DXGI_FORMAT_R32G32B32A32_FLOAT);

			D3D11_TEXTURE2D_DESC readbackDesc = telemetryStats->desc;
			readbackDesc.Usage = D3D11_USAGE_STAGING;
			readbackDesc.BindFlags = 0;
			readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			readbackDesc.MiscFlags = 0;
			winrt::com_ptr<ID3D11Texture2D> telemetryReadback;
			DX::ThrowIfFailed(cs::util::GetD3DDevice()->CreateTexture2D(
				&readbackDesc, nullptr, telemetryReadback.put()));

			_resourcesReady.store(false, std::memory_order_release);
			_wetnessMask = std::move(wetnessMask);
			_telemetryStats = std::move(telemetryStats);
			_telemetryReadback = std::move(telemetryReadback);
			_telemetryPending = false;
			_telemetryReady.store(false, std::memory_order_relaxed);
			_wetnessMean.store(0.0f, std::memory_order_relaxed);
			_wetnessCoverage.store(0.0f, std::memory_order_relaxed);
			_allocWidth = width;
			_allocHeight = height;
			_resourcesReady.store(true, std::memory_order_release);
			L->info("Wetness resources ready ({}x{}).", _allocWidth, _allocHeight);
			return true;
		} catch (const std::exception& e) {
			if (hadResources) {
				L->error("Wetness resource resize failed: {}", e.what());
			} else {
				_resourceInitFailed.store(true, std::memory_order_release);
				_wetnessMask.reset();
				_telemetryStats.reset();
				_telemetryReadback = nullptr;
				_telemetryPending = false;
				_allocWidth = 0;
				_allocHeight = 0;
				L->error("Wetness resource creation failed: {}", e.what());
			}
			return false;
		}
	}

	void WetnessEffects::OnComputeWetness()
	{
		_dispatchesLastFrame.store(0, std::memory_order_relaxed);
		_sunHookCalls.store(0, std::memory_order_relaxed);
		_sunHookNoShader.store(0, std::memory_order_relaxed);
		_sunHookMatched.store(0, std::memory_order_relaxed);
		_sunHookUnmatched.store(0, std::memory_order_relaxed);
		_maskBinds.store(0, std::memory_order_relaxed);
		_workingWidth.store(0, std::memory_order_relaxed);
		_workingHeight.store(0, std::memory_order_relaxed);
		if (!_started.load(std::memory_order_acquire)) {
			return;
		}
		const bool resourcesReady = EnsureResources();

		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !_wetnessMask) {
			return;
		}

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}
		// Clear the mask to 0 (dry) EVERY frame, BEFORE the enabled gate. The
		// WETNESS_EFFECTS define is baked at startup and the bind force-binds, so a
		// runtime disable must leave the mask reading 0 (else the shader samples a
		// frozen stale mask -> the "leftover mask stuck on screen" artifact).
		const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearUnorderedAccessViewFloat(_wetnessMask->uav.get(), zero);
		if (!_settings.enabled || !resourcesReady) {
			return;
		}
		PollTelemetry(context);

		auto* state = cs::engine::GetGraphicsState();
		auto* rtm = cs::engine::GetRenderTargetManager();
		if (!state || !rtm || !_wetnessCS || !_wetnessCB) {
			return;
		}

		const auto scaledExtent = [](std::uint32_t a_extent, float a_ratio) {
			if (a_ratio <= 0.0f) {
				return 0u;
			}
			return std::min(
				a_extent,
				static_cast<std::uint32_t>(static_cast<float>(a_extent) * a_ratio));
		};
		const std::uint32_t width = scaledExtent(_allocWidth, rtm->GetDynamicWidthRatio());
		const std::uint32_t height = scaledExtent(_allocHeight, rtm->GetDynamicHeightRatio());
		auto* normalSRV =
			cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
		auto* depthSRV = cs::engine::GetSceneDepthSRV();
		auto* sceneCamera = RE::Main::WorldRootCamera();
		if (width == 0 || height == 0 || !normalSRV || !depthSRV || !sceneCamera) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		const bool isExterior = cell && cell->IsExterior();
		const auto weather = cs::engine::SnapshotWeather();
		const float transition = std::clamp(weather.transitionPct, 0.0f, 1.0f);
		const float previous = weather.previousIsRain ? 1.0f : 0.0f;
		const float current = weather.currentIsRain ? 1.0f : 0.0f;
		const float wetnessScalar = isExterior ? std::lerp(previous, current, transition) : 0.0f;
		_isExterior.store(isExterior, std::memory_order_relaxed);
		_rainIntensity.store(wetnessScalar, std::memory_order_relaxed);

		try {
			WetnessCB cb{};
			cb.Extent[0] = width;
			cb.Extent[1] = height;
			cb.WetnessScalar = wetnessScalar;
			StoreViewToWorld(sceneCamera->world.rotate, cb.ViewToWorld);
			_wetnessCB->Update(cb);

			cs::engine::ComputeOMScope scope(context);

			ID3D11ShaderResourceView* srvs[2] = { normalSRV, depthSRV };
			ID3D11Buffer* buffers[1] = { _wetnessCB->CB() };
			ID3D11UnorderedAccessView* uavs[1] = { _wetnessMask->uav.get() };
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetConstantBuffers(0, 1, buffers);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(_wetnessCS.get(), nullptr, 0);
			context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
			_dispatchesLastFrame.fetch_add(1, std::memory_order_relaxed);
			_workingWidth.store(width, std::memory_order_relaxed);
			_workingHeight.store(height, std::memory_order_relaxed);

			ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
			ID3D11Buffer* nullBuffers[1] = { nullptr };
			ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
			context->CSSetShaderResources(0, 2, nullSRVs);
			context->CSSetConstantBuffers(0, 1, nullBuffers);
			context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
			context->CSSetShader(nullptr, nullptr, 0);

			QueueTelemetry(
				context,
				width,
				height,
				static_cast<std::uint32_t>(state->frameCount));
		} catch (const std::exception& e) {
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::err,
				"Wetness-mask dispatch failed: {}.",
				e.what());
		}
	}

	void WetnessEffects::OnPreSunLightDraw()
	{
		if (!_started.load(std::memory_order_acquire)) {
			return;
		}
		_sunHookCalls.fetch_add(1, std::memory_order_relaxed);
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}

		winrt::com_ptr<ID3D11PixelShader> boundShader;
		context->PSGetShader(boundShader.put(), nullptr, nullptr);
		if (!boundShader) {
			_sunHookNoShader.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		constexpr std::array directionalTargets{
			cs::engine::ShaderInjectionTarget::kBsdfLightDeferredDirectional,
			cs::engine::ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl
		};
		for (const auto target : directionalTargets) {
			if (boundShader.get() == cs::engine::GetInjectedPixelShader(target)) {
				_sunHookMatched.fetch_add(1, std::memory_order_relaxed);
				cs::engine::DispatchShaderInjections(target, context);
				return;
			}
		}
		_sunHookUnmatched.fetch_add(1, std::memory_order_relaxed);
	}

	void WetnessEffects::OnAmbientPassDispatch()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ?
			reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			nullptr;
		cs::engine::DispatchShaderInjections(
			cs::engine::ShaderInjectionTarget::kAmbientIblPass,
			context);
	}

	void WetnessEffects::BindWetnessMask(
		ID3D11DeviceContext* a_context,
		std::uint32_t a_slot)
	{
		if (!a_context ||
			!_resourcesReady.load(std::memory_order_acquire) ||
			!_wetnessMask) {
			return;
		}

		// Force-bind (mirror SGGI's OnAmbientPassInjection). The old SSS-style
		// "skip if slot occupied" guard was WRONG here: t4/t13 retain stale SRVs
		// from prior draws, so skipping made the injected shader read that stale
		// texture as `wetness` (garbage != 0), breaking the wet=0 no-op. Our
		// dispatch is already pass-discriminated, so unconditionally binding our
		// mask is correct and keeps dry == stock.
		auto* srv = _wetnessMask->srv.get();
		a_context->PSSetShaderResources(a_slot, 1, &srv);
		_maskBinds.fetch_add(1, std::memory_order_relaxed);
		if (a_slot == kMaskPSSlotDirectional) {
			_directionalMaskBound.store(true, std::memory_order_relaxed);
		} else if (a_slot == kMaskPSSlotAmbient) {
			_ambientMaskBound.store(true, std::memory_order_relaxed);
		}
	}

	void WetnessEffects::RestoreWetnessMaskBindings()
	{
		const bool restoreDirectional =
			_directionalMaskBound.exchange(false, std::memory_order_relaxed);
		const bool restoreAmbient =
			_ambientMaskBound.exchange(false, std::memory_order_relaxed);
		if (!restoreDirectional && !restoreAmbient) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}

		ID3D11ShaderResourceView* nullSRV = nullptr;
		if (restoreDirectional) {
			context->PSSetShaderResources(kMaskPSSlotDirectional, 1, &nullSRV);
		}
		if (restoreAmbient) {
			context->PSSetShaderResources(kMaskPSSlotAmbient, 1, &nullSRV);
		}
	}

	void WetnessEffects::PollTelemetry(ID3D11DeviceContext* a_context)
	{
		if (!_telemetryPending || !_telemetryReadback) {
			return;
		}

		D3D11_MAPPED_SUBRESOURCE mapped{};
		const HRESULT result = a_context->Map(
			_telemetryReadback.get(),
			0,
			D3D11_MAP_READ,
			D3D11_MAP_FLAG_DO_NOT_WAIT,
			&mapped);
		if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
			return;
		}
		if (FAILED(result)) {
			_telemetryPending = false;
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"Wetness telemetry readback failed: 0x{:08X}.",
				static_cast<std::uint32_t>(result));
			return;
		}

		double sum = 0.0;
		double covered = 0.0;
		double count = 0.0;
		for (std::uint32_t y = 0; y < kTelemetryHeight; ++y) {
			const auto* row = reinterpret_cast<const float*>(
				static_cast<const std::byte*>(mapped.pData) +
				static_cast<std::size_t>(y) * mapped.RowPitch);
			for (std::uint32_t x = 0; x < kTelemetryWidth; ++x) {
				const float* stats = row + static_cast<std::size_t>(x) * 4;
				if (std::isfinite(stats[0]) &&
					std::isfinite(stats[1]) &&
					std::isfinite(stats[2])) {
					sum += stats[0];
					covered += stats[1];
					count += stats[2];
				}
			}
		}
		a_context->Unmap(_telemetryReadback.get(), 0);
		_telemetryPending = false;

		if (count > 0.0) {
			_wetnessMean.store(static_cast<float>(sum / count), std::memory_order_relaxed);
			_wetnessCoverage.store(
				static_cast<float>(covered / count), std::memory_order_relaxed);
			_telemetryReady.store(true, std::memory_order_release);
		}
	}

	void WetnessEffects::QueueTelemetry(
		ID3D11DeviceContext* a_context,
		std::uint32_t a_sourceWidth,
		std::uint32_t a_sourceHeight,
		std::uint32_t a_frameIndex)
	{
		if (_telemetryPending ||
			!_telemetryCS ||
			!_telemetryCB ||
			!_telemetryStats ||
			!_telemetryReadback) {
			return;
		}
		if (_telemetryReady.load(std::memory_order_acquire) &&
			a_frameIndex - _telemetryLastQueuedFrame < kTelemetryIntervalFrames) {
			return;
		}

		TelemetryCB cb{};
		cb.SourceExtent[0] = a_sourceWidth;
		cb.SourceExtent[1] = a_sourceHeight;
		cb.OutputExtent[0] = kTelemetryWidth;
		cb.OutputExtent[1] = kTelemetryHeight;
		_telemetryCB->Update(cb);

		ID3D11ShaderResourceView* srvs[1] = { _wetnessMask->srv.get() };
		ID3D11Buffer* buffers[1] = { _telemetryCB->CB() };
		ID3D11UnorderedAccessView* uavs[1] = { _telemetryStats->uav.get() };
		a_context->CSSetShaderResources(0, 1, srvs);
		a_context->CSSetConstantBuffers(0, 1, buffers);
		a_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		a_context->CSSetShader(_telemetryCS.get(), nullptr, 0);
		a_context->Dispatch(
			(kTelemetryWidth + 7u) / 8u,
			(kTelemetryHeight + 7u) / 8u,
			1);

		ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
		ID3D11Buffer* nullBuffers[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
		a_context->CSSetShaderResources(0, 1, nullSRVs);
		a_context->CSSetConstantBuffers(0, 1, nullBuffers);
		a_context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		a_context->CSSetShader(nullptr, nullptr, 0);
		a_context->CopyResource(_telemetryReadback.get(), _telemetryStats->resource.get());
		_telemetryPending = true;
		_telemetryLastQueuedFrame = a_frameIndex;
	}

	void WetnessEffects::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("is_exterior", _isExterior.load(std::memory_order_relaxed))
			.Field(
				"rain_intensity",
				static_cast<double>(_rainIntensity.load(std::memory_order_relaxed)))
			.Field(
				"wetness_mean",
				static_cast<double>(_wetnessMean.load(std::memory_order_relaxed)))
			.Field(
				"wetness_coverage",
				static_cast<double>(_wetnessCoverage.load(std::memory_order_relaxed)))
			.Field(
				"dispatches",
				static_cast<std::int64_t>(
					_dispatchesLastFrame.load(std::memory_order_relaxed)))
			.Dimensions(
				"working",
				_workingWidth.load(std::memory_order_relaxed),
				_workingHeight.load(std::memory_order_relaxed))
			.Field(
				"sun_hook_calls",
				static_cast<std::int64_t>(_sunHookCalls.load(std::memory_order_relaxed)))
			.Field(
				"sun_hook_no_shader",
				static_cast<std::int64_t>(_sunHookNoShader.load(std::memory_order_relaxed)))
			.Field(
				"sun_hook_matched",
				static_cast<std::int64_t>(_sunHookMatched.load(std::memory_order_relaxed)))
			.Field(
				"sun_hook_unmatched",
				static_cast<std::int64_t>(_sunHookUnmatched.load(std::memory_order_relaxed)))
			.Field(
				"mask_binds",
				static_cast<std::int64_t>(_maskBinds.load(std::memory_order_relaxed)));
	}

	void WetnessEffects::DrawSettings()
	{
		if (ImGui::Checkbox("Enabled", &_settings.enabled)) {
			SaveSettings();
		}

		const char* status = _resourceInitFailed.load(std::memory_order_acquire) ? "failed" :
			(_resourcesReady.load(std::memory_order_acquire) ? "ready" : "not ready");
		ImGui::TextDisabled("Resources: %s | extent: %ux%u", status, _allocWidth, _allocHeight);
		ImGui::TextDisabled(
			"rain: %.2f | mean: %.3f | coverage: %.3f",
			_rainIntensity.load(std::memory_order_relaxed),
			_wetnessMean.load(std::memory_order_relaxed),
			_wetnessCoverage.load(std::memory_order_relaxed));

		// Debug mask preview: raw R8 wetness in-game, bright = wet, dark = dry, no
		// RenderDoc needed. Mirrors ScreenSpaceShadows' mask preview. Watch this
		// thumbnail while rotating the camera over fixed ground: a correct mask is
		// world-locked (a given surface holds its value); a camera-dependent mask
		// visibly shifts with view angle.
		static bool s_showMaskPreview = false;
		ImGui::Checkbox("Show wetness-mask preview (debug)", &s_showMaskPreview);
		if (s_showMaskPreview) {
			if (_wetnessMask && _wetnessMask->srv && _allocWidth > 0 && _allocHeight > 0) {
				const float aspect = static_cast<float>(_allocWidth) / static_cast<float>(_allocHeight);
				const float previewWidth = 480.0f;
				const float previewHeight = previewWidth / aspect;
				ImGui::TextDisabled("Mask %ux%u (bright = wet, dark = dry)", _allocWidth, _allocHeight);
				ImGui::Image(reinterpret_cast<ImTextureID>(_wetnessMask->srv.get()),
					ImVec2(previewWidth, previewHeight));
			} else {
				ImGui::TextDisabled("Mask not allocated.");
			}
		}
	}

	void WetnessEffects::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(WetnessEffects::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
