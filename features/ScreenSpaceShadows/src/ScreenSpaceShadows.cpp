#include "ScreenSpaceShadows.h"

#include <DirectXMath.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/S/Sky.h>
#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "ScreenSpaceShadowsMath.h"
#include "Settings/FeatureConfig.h"
#include "SssMaskBinding.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/Sky.h"

#pragma warning(push)
#pragma warning(disable: 4244 4838)
#include "bend_sss_cpu.h"
#pragma warning(pop)

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenspaceshadows");

		constexpr const wchar_t* kRaymarchPath = L"Data\\Shaders\\ScreenSpaceShadows\\RaymarchCS.hlsl";

		// Engine depth uses near=0, far=1.
		constexpr float kFarDepthValue = 1.0f;
		constexpr float kNearDepthValue = 0.0f;

		std::string SettingError(std::string_view a_key, std::string_view a_reason)
		{
			return "settings." + std::string(a_key) + ": " + std::string(a_reason);
		}

		bool AcceptSetting(
			feature_config::ScalarReadStatus a_status,
			std::string_view a_key,
			std::string_view a_expected,
			std::string& a_error)
		{
			switch (a_status) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = SettingError(a_key, "expected " + std::string(a_expected));
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = SettingError(a_key, "invalid value");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = SettingError(a_key, "value is out of range");
				break;
			}
			return false;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			ScreenSpaceShadows::Settings& a_candidate,
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

			if (!AcceptSetting(feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled),
					"enabled", "boolean", a_error)
				|| !AcceptSetting(feature_config::ReadFloat(*settingsTable, "surface_thickness", a_candidate.surfaceThickness, 0.005f, 0.05f),
					"surface_thickness", "number", a_error)
				|| !AcceptSetting(feature_config::ReadFloat(*settingsTable, "bilinear_threshold", a_candidate.bilinearThreshold, 0.02f, 1.0f),
					"bilinear_threshold", "number", a_error)
				|| !AcceptSetting(feature_config::ReadFloat(*settingsTable, "shadow_contrast", a_candidate.shadowContrast, 0.0f, 4.0f),
					"shadow_contrast", "number", a_error)) {
				return false;
			}

			auto sampleCount = static_cast<std::uint64_t>(a_candidate.sampleCount);
			const auto sampleCountStatus = feature_config::ReadUnsignedInteger(
				*settingsTable,
				"sample_count",
				sampleCount,
				sss_math::kMinSampleMultiplier,
				sss_math::kMaxSampleMultiplier);
			if (!AcceptSetting(sampleCountStatus, "sample_count", "integer", a_error)) {
				return false;
			}
			if (sampleCountStatus == feature_config::ScalarReadStatus::kValid) {
				a_candidate.sampleCount = static_cast<std::uint32_t>(sampleCount);
				return true;
			}

			float legacyPercent = 0.0f;
			const auto legacyStatus = feature_config::ReadFloat(
				*settingsTable,
				"max_shadow_length_percent",
				legacyPercent,
				0.5f,
				15.0f);
			if (!AcceptSetting(legacyStatus, "max_shadow_length_percent", "number", a_error)) {
				return false;
			}
			if (legacyStatus == feature_config::ScalarReadStatus::kValid) {
				a_candidate.sampleCount = sss_math::MigrateLegacyShadowLengthPercent(legacyPercent);
				L->info(
					"Mapped legacy max_shadow_length_percent={} to sample_count={}; save settings to persist the canonical key.",
					legacyPercent,
					a_candidate.sampleCount);
			}

			return true;
		}

		void GetPixelShaderResource(
			void* a_context,
			std::uint32_t a_slot,
			ID3D11ShaderResourceView** a_view) noexcept
		{
			static_cast<ID3D11DeviceContext*>(a_context)->PSGetShaderResources(
				static_cast<UINT>(a_slot),
				1,
				a_view);
		}

		void SetPixelShaderResource(
			void* a_context,
			std::uint32_t a_slot,
			ID3D11ShaderResourceView* a_view) noexcept
		{
			static_cast<ID3D11DeviceContext*>(a_context)->PSSetShaderResources(
				static_cast<UINT>(a_slot),
				1,
				&a_view);
		}
	}

	ScreenSpaceShadows* ScreenSpaceShadows::GetSingleton()
	{
		static ScreenSpaceShadows instance;
		return &instance;
	}

	std::span<const FeatureDebugView>
		ScreenSpaceShadows::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "shadow_mask",
				.label = "Shadow-mask preview",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const ScreenSpaceShadows&>(a_feature)
						.GetShadowMaskDebugTexture();
				}
			}
		};
		return views;
	}

	void ScreenSpaceShadows::SetDebugView(std::string_view a_view) noexcept
	{
		_debugPreviewEnabled.store(
			a_view == "shadow_mask",
			std::memory_order_release);
	}

	FeatureDebugTexture
		ScreenSpaceShadows::GetShadowMaskDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Mask not allocated."
		};
		if (!_debugPreviewEnabled.load(std::memory_order_acquire)
			|| !_maskTexture
			|| !_maskTexture->srv
			|| _allocWidth == 0
			|| _allocHeight == 0) {
			return texture;
		}
		texture.texture = _maskTexture->srv.get();
		texture.width = _allocWidth;
		texture.height = _allocHeight;
		texture.caption = std::format(
			"Mask {}x{} (bright = lit, dark = shadowed)",
			_allocWidth,
			_allocHeight);
		return texture;
	}

	bool ScreenSpaceShadows::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		return true;
	}

	void ScreenSpaceShadows::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("surface_thickness", _settings.surfaceThickness);
		settings.insert_or_assign("bilinear_threshold", _settings.bilinearThreshold);
		settings.insert_or_assign("shadow_contrast", _settings.shadowContrast);
		settings.insert_or_assign("sample_count", static_cast<std::int64_t>(_settings.sampleCount));

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settings); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void ScreenSpaceShadows::Load()
	{
		const bool replacementRegistered =
			cs::engine::RegisterReplacement({
				.targetId = cs::engine::ShaderInjectionTarget::kBsdfLight,
				.contributor = "ScreenSpaceShadows",
				.defines = {
					{
						cs::engine::shader_injection_defines::
							kScreenSpaceShadows,
						"1"
					}
				},
				.isReady = [this] {
					return IsShadowMaskReady();
				},
				.bind = [this](ID3D11DeviceContext* a_context) {
					BindShadowMask(a_context);
				},
				.slotClaims = {
					{
						.stage = cs::engine::ShaderStage::kPixel,
						.resourceType = cs::engine::ShaderResourceType::kShaderResource,
						.slot = kMaskPSSlot
					}
				}
			});
		if (!replacementRegistered) {
			FailLoad("Failed to register the ScreenSpaceShadows BSDF light shader replacement");
			return;
		}

		cs::engine::RegisterPreDeferredLightsImpl([] {
			ScreenSpaceShadows::GetSingleton()->OnPreDeferredLights();
		});
		cs::engine::RegisterPostDeferredLightsImpl([] {
			ScreenSpaceShadows::GetSingleton()->OnPostDeferredLights();
		});
		_started.store(true, std::memory_order_release);
		L->info(
			"Registered deferred-lights callbacks (enabled={}).",
			_settings.enabled);
	}

	void ScreenSpaceShadows::CreateMaskTexture(std::uint32_t a_width, std::uint32_t a_height)
	{
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = a_width;
		textureDesc.Height = a_height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		auto texture = std::make_unique<cs::buffer::Texture2D>(textureDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		texture->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_R8_UNORM;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		texture->CreateUAV(uavDesc);

		_maskTexture = std::move(texture);
		_allocWidth = a_width;
		_allocHeight = a_height;
	}

	bool ScreenSpaceShadows::TryGetMaskExtents(
		sss_mask_binding::Extent& a_required,
		sss_mask_binding::Extent& a_allocation) const
	{
		auto* state = cs::engine::GetGraphicsState();
		if (!state || state->screenWidth == 0 || state->screenHeight == 0)
			return false;
		float widthRatio = 1.0f;
		float heightRatio = 1.0f;
		if (auto* rtm = cs::engine::GetRenderTargetManager()) {
			widthRatio = rtm->GetDynamicWidthRatio();
			heightRatio = rtm->GetDynamicHeightRatio();
		}
		if (!std::isfinite(widthRatio) || widthRatio <= 0.0f)
			widthRatio = 1.0f;
		if (!std::isfinite(heightRatio) || heightRatio <= 0.0f)
			heightRatio = 1.0f;
		const auto requiredWidth = std::ceil(
			static_cast<double>(state->screenWidth)
			* static_cast<double>(widthRatio));
		const auto requiredHeight = std::ceil(
			static_cast<double>(state->screenHeight)
			* static_cast<double>(heightRatio));
		if (requiredWidth < 1.0
			|| requiredHeight < 1.0
			|| requiredWidth
				> D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
			|| requiredHeight
				> D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
			return false;
		}
		a_required = {
			static_cast<std::uint32_t>(requiredWidth),
			static_cast<std::uint32_t>(requiredHeight)
		};
		a_allocation = {
			std::max(state->screenWidth, a_required.width),
			std::max(state->screenHeight, a_required.height)
		};
		return true;
	}

	bool ScreenSpaceShadows::CreateWhiteFallback(
		ID3D11Device* a_device,
		sss_mask_binding::Extent a_allocation)
	{
		if (!a_device
			|| a_allocation.width == 0
			|| a_allocation.height == 0
			|| a_allocation.width
				> D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
			|| a_allocation.height
				> D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
			|| a_allocation.width
				> std::numeric_limits<std::size_t>::max()
					/ a_allocation.height) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::err,
					"Rejected invalid full-extent white SSS fallback allocation ({}x{}).",
					a_allocation.width,
					a_allocation.height);
			}
			return false;
		}

		const auto pixelCount =
			static_cast<std::size_t>(a_allocation.width)
			* a_allocation.height;
		std::vector<std::uint8_t> white;
		try {
			white.assign(
				pixelCount,
				sss_mask_binding::kWhiteR8Unorm);
		} catch (...) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::err,
					"Failed to allocate CPU initialization bytes for the full-extent white SSS fallback ({}x{}).",
					a_allocation.width,
					a_allocation.height);
			}
			return false;
		}

		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = a_allocation.width;
		textureDesc.Height = a_allocation.height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA initialData{};
		initialData.pSysMem = white.data();
		initialData.SysMemPitch = a_allocation.width;
		initialData.SysMemSlicePitch =
			static_cast<UINT>(pixelCount);

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_device->CreateTexture2D(
				&textureDesc,
				&initialData,
				texture.put()))) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::err,
					"Failed to create the full-extent white SSS fallback texture ({}x{}).",
					a_allocation.width,
					a_allocation.height);
			}
			return false;
		}
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (FAILED(a_device->CreateShaderResourceView(
				texture.get(),
				nullptr,
				srv.put()))) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::err,
					"Failed to create the full-extent white SSS fallback SRV ({}x{}).",
					a_allocation.width,
					a_allocation.height);
			}
			return false;
		}
		_whiteFallbackSRV = std::move(srv);
		(void)_whiteFallbackExtent.CompleteAllocation(
			a_allocation,
			true);
		return true;
	}

	bool ScreenSpaceShadows::EnsureWhiteFallback(
		ID3D11Device* a_device,
		sss_mask_binding::Extent a_required,
		sss_mask_binding::Extent a_allocation)
	{
		const sss_mask_binding::FallbackAllocationKey key{
			a_allocation,
			_deviceGeneration
		};
		const bool needsAllocation =
			!_whiteFallbackSRV
			|| _whiteFallbackExtent.allocated != a_allocation;
		if (!needsAllocation) {
			if (_whiteFallbackBackoff.HasFailure()
				&& !_whiteFallbackBackoff.IsLatchedFor(key)) {
				_whiteFallbackBackoff.RecordSuccess();
			}
		} else if (_whiteFallbackBackoff.ShouldAttempt(key)) {
			_whiteFallbackAllocationAttempts.fetch_add(
				1,
				std::memory_order_relaxed);
			if (CreateWhiteFallback(a_device, a_allocation)) {
				_whiteFallbackBackoff.RecordSuccess();
			} else {
				_whiteFallbackBackoff.RecordFailure(key);
				_whiteFallbackAllocationFailures.fetch_add(
					1,
					std::memory_order_relaxed);
			}
		}
		const bool ready =
			_whiteFallbackSRV
			&& _whiteFallbackExtent.IsCompatible(a_required);
		_whiteFallbackFailureLatchedTelemetry.store(
			needsAllocation
				&& _whiteFallbackBackoff.IsLatchedFor(key),
			std::memory_order_relaxed);
		_whiteFallbackWidthTelemetry.store(
			_whiteFallbackExtent.allocated.width,
			std::memory_order_relaxed);
		_whiteFallbackHeightTelemetry.store(
			_whiteFallbackExtent.allocated.height,
			std::memory_order_relaxed);
		_whiteFallbackReady.store(ready, std::memory_order_release);
		return ready;
	}

	void ScreenSpaceShadows::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_started.load(std::memory_order_acquire) || !a_device) return;
		if (_deviceIdentity != a_device) {
			_deviceIdentity = a_device;
			++_deviceGeneration;
			_resourcesReady.store(false, std::memory_order_release);
			_whiteFallbackReady.store(false, std::memory_order_release);
			_resourceInitFailed = false;
			_realMaskReadyForDraw = false;
			_raymarchCB.reset();
			_maskTexture.reset();
			_pointBorderSampler = nullptr;
			_raymarchCS = nullptr;
			_whiteFallbackSRV = nullptr;
			_allocWidth = 0;
			_allocHeight = 0;
			_lastCompiledSampleCount = 0;
			_whiteFallbackExtent = {};
			_whiteFallbackBackoff.RecordSuccess();
			_maskBound.store(false, std::memory_order_relaxed);
		}
		sss_mask_binding::Extent required;
		sss_mask_binding::Extent allocation;
		if (TryGetMaskExtents(required, allocation)) {
			(void)EnsureWhiteFallback(
				a_device,
				required,
				allocation);
		}
		_requiredMaskExtent = required;
		_requiredMaskWidthTelemetry.store(
			required.width,
			std::memory_order_relaxed);
		_requiredMaskHeightTelemetry.store(
			required.height,
			std::memory_order_relaxed);
		(void)EnsureResources();
	}

	bool ScreenSpaceShadows::IsShadowMaskReady()
	{
		if (!_started.load(std::memory_order_acquire))
			return false;
		(void)EnsureResources();
		sss_mask_binding::Extent required;
		sss_mask_binding::Extent allocation;
		auto* device = cs::util::GetD3DDevice();
		const bool fallbackReady =
			device
			&& TryGetMaskExtents(required, allocation)
			&& EnsureWhiteFallback(
				device,
				required,
				allocation);
		_requiredMaskExtent = required;
		_requiredMaskWidthTelemetry.store(
			required.width,
			std::memory_order_relaxed);
		_requiredMaskHeightTelemetry.store(
			required.height,
			std::memory_order_relaxed);
		return fallbackReady;
	}

	cs::ScreenSpaceShadowsFeatureData ScreenSpaceShadows::GetCommonBufferData() const
	{
		// cached readiness avoids IsShadowMaskReady allocation
		if (!_started.load(std::memory_order_acquire)
			|| !_resourcesReady.load(std::memory_order_acquire)) {
			return {};
		}
		return {
			.EnableScreenSpaceShadows =
				_settings.enabled ?
				1u :
				0u,
			.ShadowContrast = _settings.shadowContrast
		};
	}

	bool ScreenSpaceShadows::EnsureResources()
	{
		if (_resourcesReady.load(std::memory_order_acquire)) {
			return true;
		}
		if (_resourceInitFailed) {
			return false;
		}

		auto* device = cs::util::GetD3DDevice();
		if (!device) {
			return false;
		}

		try {
			_raymarchCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<RaymarchCB>());

			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
			samplerDesc.MaxAnisotropy = 1;
			samplerDesc.BorderColor[0] = kFarDepthValue;
			samplerDesc.BorderColor[1] = kFarDepthValue;
			samplerDesc.BorderColor[2] = kFarDepthValue;
			samplerDesc.BorderColor[3] = kFarDepthValue;
			samplerDesc.MinLOD = 0.0f;
			samplerDesc.MaxLOD = FLT_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, _pointBorderSampler.put()));

			sss_mask_binding::Extent required;
			sss_mask_binding::Extent allocation;
			if (!TryGetMaskExtents(required, allocation)) {
				throw std::runtime_error("graphics state has no screen dimensions");
			}
			CreateMaskTexture(allocation.width, allocation.height);
			_resourcesReady.store(true, std::memory_order_release);
			L->info("Resources ready ({}x{}).", _allocWidth, _allocHeight);
			return true;
		} catch (const std::exception& e) {
			_resourceInitFailed = true;
			_pointBorderSampler = nullptr;
			_maskTexture.reset();
			_raymarchCB.reset();
			_allocWidth = 0;
			_allocHeight = 0;
			L->error("Resource creation failed: {}", e.what());
			return false;
		} catch (...) {
			_resourceInitFailed = true;
			_pointBorderSampler = nullptr;
			_maskTexture.reset();
			_raymarchCB.reset();
			_allocWidth = 0;
			_allocHeight = 0;
			L->error("Resource creation failed.");
			return false;
		}
	}

	std::uint32_t ScreenSpaceShadows::GetScaledSampleCount() const
	{
		auto* state = cs::engine::GetGraphicsState();
		auto* rtm = cs::engine::GetRenderTargetManager();
		if (!state || !rtm) {
			return sss_math::kMinSampleCount;
		}

		const float width = static_cast<float>(state->screenWidth) * rtm->GetDynamicWidthRatio();
		const float height = static_cast<float>(state->screenHeight) * rtm->GetDynamicHeightRatio();
		return sss_math::ScaleSampleCount(_settings.sampleCount, width, height);
	}

	ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarch()
	{
		const auto scaledSampleCount = GetScaledSampleCount();
		if (scaledSampleCount != _lastCompiledSampleCount) {
			_lastCompiledSampleCount = scaledSampleCount;
			_raymarchCS = nullptr;
		}

		if (!_raymarchCS) {
			const auto sampleCount = std::to_string(scaledSampleCount);
			const std::vector<std::pair<const char*, const char*>> defines{
				{ "SAMPLE_COUNT", sampleCount.c_str() }
			};
			_raymarchCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(kRaymarchPath, defines, "cs_5_0")));
			if (_raymarchCS) {
				L->info("Compiled raymarch shader with {} samples.", scaledSampleCount);
			}
		}
		return _raymarchCS.get();
	}

	void ScreenSpaceShadows::OnPreDeferredLights()
	{
		_realMaskReadyForDraw = false;
		if (!_started.load(std::memory_order_acquire)) {
			return;
		}

		_dispatchedLastFrame.store(0, std::memory_order_relaxed);
		_maskBoundLastFrame.store(false, std::memory_order_relaxed);
		sss_mask_binding::Extent required;
		sss_mask_binding::Extent allocation;
		auto* device = cs::util::GetD3DDevice();
		const bool extentsValid =
			device && TryGetMaskExtents(required, allocation);
		_requiredMaskExtent = required;
		_requiredMaskWidthTelemetry.store(
			required.width,
			std::memory_order_relaxed);
		_requiredMaskHeightTelemetry.store(
			required.height,
			std::memory_order_relaxed);
		if (extentsValid) {
			(void)EnsureWhiteFallback(
				device,
				required,
				allocation);
		}
		if (!extentsValid)
			return;
		if (!EnsureResources()) {
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

		const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		auto* state = cs::engine::GetGraphicsState();
		if (!state || state->screenWidth == 0 || state->screenHeight == 0) {
			return;
		}
		if (allocation.width != _allocWidth
			|| allocation.height != _allocHeight) {
			try {
				CreateMaskTexture(
					allocation.width,
					allocation.height);
			} catch (const std::exception& e) {
				L->error("Mask resize failed: {}", e.what());
			} catch (...) {
				L->error("Mask resize failed.");
			}
		}
		const sss_mask_binding::Extent realExtent{
			_allocWidth,
			_allocHeight
		};
		if (_maskTexture
			&& _maskTexture->uav
			&& sss_mask_binding::Covers(realExtent, required)) {
			context->ClearUnorderedAccessViewFloat(
				_maskTexture->uav.get(),
				white);
			_realMaskReadyForDraw = true;
		}
		if (!_realMaskReadyForDraw)
			return;

		try {
			auto* sky = RE::Sky::GetSingleton();
			auto* shader = (_settings.enabled && sky && sky->mode.get() == RE::Sky::Mode::kFull) ?
				GetComputeRaymarch() : nullptr;

			float sx = 0.0f;
			float sy = 0.0f;
			float sz = 0.0f;
			if (shader && cs::engine::TryGetSunDirectionWS(sx, sy, sz)) {
				auto* depthSRV = cs::engine::GetSceneDepthSRV();
				auto* rtm = cs::engine::GetRenderTargetManager();
				if (depthSRV && rtm && _raymarchCB && _pointBorderSampler) {
					const float widthRatio = rtm->GetDynamicWidthRatio();
					const float heightRatio = rtm->GetDynamicHeightRatio();
					const float renderWidth = static_cast<float>(state->screenWidth) * widthRatio;
					const float renderHeight = static_cast<float>(state->screenHeight) * heightRatio;
					int viewportSize[2] = {
						static_cast<int>(renderWidth),
						static_cast<int>(renderHeight)
					};

					if (viewportSize[0] > 0 && viewportSize[1] > 0) {
						cs::engine::ComputeOMScope scope(context);

						// Negate sunlight; transpose WorldRootCamera, not camViewData.
						auto* sceneCamera = cs::engine::GetWorldRootCamera();
						if (!sceneCamera) {
							return;
						}
						DirectX::XMVECTOR sunDir = DirectX::XMVectorSet(-sx, -sy, -sz, 0.0f);
						DirectX::XMMATRIX vp = DirectX::XMLoadFloat4x4(
							reinterpret_cast<const DirectX::XMFLOAT4X4*>(&sceneCamera->worldToCam));
						DirectX::XMVECTOR clip = DirectX::XMVector4Transform(sunDir, DirectX::XMMatrixTranspose(vp));
						float lightProj[4] = {
							DirectX::XMVectorGetX(clip),
							DirectX::XMVectorGetY(clip),
							DirectX::XMVectorGetZ(clip),
							DirectX::XMVectorGetW(clip)
						};

						int minBounds[2] = { 0, 0 };
						int maxBounds[2] = { viewportSize[0], viewportSize[1] };
						auto dispatchList = Bend::BuildDispatchList(lightProj, viewportSize, minBounds, maxBounds);

						_sunX.store(sx, std::memory_order_relaxed);
						_sunY.store(sy, std::memory_order_relaxed);
						_sunZ.store(sz, std::memory_order_relaxed);
						_lightX.store(dispatchList.LightCoordinate_Shader[0], std::memory_order_relaxed);
						_lightY.store(dispatchList.LightCoordinate_Shader[1], std::memory_order_relaxed);
						_clipW.store(lightProj[3], std::memory_order_relaxed);

						// Bend handles both sun directions; degenerate frames stay lit.
						{
							ID3D11ShaderResourceView* srvs[1] = { depthSRV };
							ID3D11UnorderedAccessView* uavs[1] = { _maskTexture->uav.get() };
							ID3D11SamplerState* samplers[1] = { _pointBorderSampler.get() };
							ID3D11Buffer* buffers[1] = { _raymarchCB->CB() };
							context->CSSetShaderResources(0, 1, srvs);
							context->CSSetSamplers(0, 1, samplers);
							context->CSSetConstantBuffers(1, 1, buffers);
							context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
							context->CSSetShader(shader, nullptr, 0);

							for (int i = 0; i < dispatchList.DispatchCount; ++i) {
								const auto& dispatch = dispatchList.Dispatch[i];
								RaymarchCB cb{};
								std::copy_n(dispatchList.LightCoordinate_Shader, 4, cb.LightCoordinate);
								std::copy_n(dispatch.WaveOffset_Shader, 2, cb.WaveOffset);
								cb.FarDepthValue = kFarDepthValue;
								cb.NearDepthValue = kNearDepthValue;
								cb.InvDepthTextureSize[0] = 1.0f / static_cast<float>(viewportSize[0]);
								cb.InvDepthTextureSize[1] = 1.0f / static_cast<float>(viewportSize[1]);
								cb.DynamicRes[0] = widthRatio;
								cb.DynamicRes[1] = heightRatio;
								cb.SurfaceThickness = _settings.surfaceThickness;
								cb.BilinearThreshold = _settings.bilinearThreshold;
								cb.ShadowContrast = _settings.shadowContrast;
								_raymarchCB->Update(cb);
								context->Dispatch(
									static_cast<UINT>(dispatch.WaveCount[0]),
									static_cast<UINT>(dispatch.WaveCount[1]),
									static_cast<UINT>(dispatch.WaveCount[2]));
								_dispatchedLastFrame.fetch_add(1, std::memory_order_relaxed);
							}

							ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
							ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
							ID3D11SamplerState* nullSamplers[1] = { nullptr };
							ID3D11Buffer* nullBuffers[1] = { nullptr };
							context->CSSetShaderResources(0, 1, nullSRVs);
							context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
							context->CSSetShader(nullptr, nullptr, 0);
							context->CSSetSamplers(0, 1, nullSamplers);
							context->CSSetConstantBuffers(1, 1, nullBuffers);
						}
					}
				}
			}
		} catch (const std::exception& e) {
			L->error("Raymarch dispatch failed: {}", e.what());
		} catch (...) {
			L->error("Raymarch dispatch failed.");
		}
	}

	void ScreenSpaceShadows::BindShadowMask(
		ID3D11DeviceContext* a_context)
	{
		const sss_mask_binding::Api api{
			.context = a_context,
			.get = &GetPixelShaderResource,
			.set = &SetPixelShaderResource
		};
		auto* realMask =
			_maskTexture && _maskTexture->srv
			? _maskTexture->srv.get()
			: nullptr;
		const auto result = sss_mask_binding::Bind(
			api,
			kMaskPSSlot,
			realMask,
			{ _allocWidth, _allocHeight },
			_realMaskReadyForDraw,
			_whiteFallbackSRV.get(),
			_whiteFallbackExtent.allocated,
			_requiredMaskExtent);
		if (result.validBinding) {
			_maskBound.store(true, std::memory_order_relaxed);
			_maskBoundLastFrame.store(true, std::memory_order_relaxed);
		}
	}

	void ScreenSpaceShadows::OnPostDeferredLights()
	{
		// Restore plugin-owned t6 to null.
		if (!_maskBound.exchange(false, std::memory_order_relaxed)) {
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
		const sss_mask_binding::Api api{
			.context = context,
			.get = &GetPixelShaderResource,
			.set = &SetPixelShaderResource
		};
		auto* realMask =
			_maskTexture && _maskTexture->srv
			? _maskTexture->srv.get()
			: nullptr;
		(void)sss_mask_binding::RestoreNullIfOwned(
			api,
			kMaskPSSlot,
			realMask,
			_whiteFallbackSRV.get());
	}

	void ScreenSpaceShadows::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("resources_ready", _resourcesReady.load(std::memory_order_acquire))
			.Field("white_fallback_ready", _whiteFallbackReady.load(std::memory_order_acquire))
			.Field(
				"white_fallback_allocation_attempts",
				static_cast<std::int64_t>(
					_whiteFallbackAllocationAttempts.load(
						std::memory_order_relaxed)))
			.Field(
				"white_fallback_allocation_failures",
				static_cast<std::int64_t>(
					_whiteFallbackAllocationFailures.load(
						std::memory_order_relaxed)))
			.Field(
				"white_fallback_failure_latched",
				_whiteFallbackFailureLatchedTelemetry.load(
					std::memory_order_relaxed))
			.Field(
				"white_fallback_stock_latched",
				_whiteFallbackFailureLatchedTelemetry.load(
					std::memory_order_relaxed)
					&& !_whiteFallbackReady.load(
						std::memory_order_acquire))
			.Field("mask_bound", _maskBoundLastFrame.load(std::memory_order_relaxed))
			.Field("dispatches", static_cast<std::int64_t>(_dispatchedLastFrame.load(std::memory_order_relaxed)))
			.Dimensions("mask", _allocWidth, _allocHeight)
			.Dimensions(
				"required_extent",
				_requiredMaskWidthTelemetry.load(std::memory_order_relaxed),
				_requiredMaskHeightTelemetry.load(std::memory_order_relaxed))
			.Dimensions(
				"white_fallback",
				_whiteFallbackWidthTelemetry.load(std::memory_order_relaxed),
				_whiteFallbackHeightTelemetry.load(std::memory_order_relaxed))
			.Field("sun_x", static_cast<double>(_sunX.load(std::memory_order_relaxed)))
			.Field("sun_y", static_cast<double>(_sunY.load(std::memory_order_relaxed)))
			.Field("sun_z", static_cast<double>(_sunZ.load(std::memory_order_relaxed)))
			.Field("light_x", static_cast<double>(_lightX.load(std::memory_order_relaxed)))
			.Field("light_y", static_cast<double>(_lightY.load(std::memory_order_relaxed)))
			.Field("clip_w", static_cast<double>(_clipW.load(std::memory_order_relaxed)));
	}

	void ScreenSpaceShadows::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);
		changed |= ImGui::SliderFloat("Surface thickness", &_settings.surfaceThickness, 0.005f, 0.05f);
		changed |= ImGui::SliderFloat("Bilinear threshold", &_settings.bilinearThreshold, 0.02f, 1.0f);
		changed |= ImGui::SliderFloat("Shadow contrast", &_settings.shadowContrast, 0.0f, 4.0f);

		auto sampleCount = static_cast<int>(_settings.sampleCount);
		if (ImGui::SliderInt("Sample count multiplier", &sampleCount, 1, 4)) {
			_settings.sampleCount = static_cast<std::uint32_t>(sampleCount);
			changed = true;
		}
		if (changed) {
			SaveSettings();
		}

		ImGui::TextDisabled(
			"Resources: %s | wave dispatches last frame: %u",
			_resourcesReady.load(std::memory_order_acquire) ? "ready" : "not ready",
			_dispatchedLastFrame.load(std::memory_order_relaxed));
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void ScreenSpaceShadows::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(ScreenSpaceShadows::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
