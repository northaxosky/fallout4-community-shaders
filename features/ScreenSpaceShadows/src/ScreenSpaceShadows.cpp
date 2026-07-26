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
#include <stdexcept>
#include <string>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "ScreenSpaceShadowsMath.h"
#include "Settings/FeatureConfig.h"
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

		constexpr const wchar_t* kRaymarchPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceShadows\\Shaders\\RaymarchCS.hlsl";

		// FO4 uses standard non-linear depth, near=0/far=1.
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
	}

	ScreenSpaceShadows* ScreenSpaceShadows::GetSingleton()
	{
		static ScreenSpaceShadows instance;
		return &instance;
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
		const auto registerDirectionalReplacement =
			[this](cs::engine::ShaderInjectionTarget a_target) {
				return cs::engine::RegisterReplacement({
					.targetId = a_target,
					.contributor = "ScreenSpaceShadows",
					.defines = { { "SCREEN_SPACE_SHADOWS", "1" } },
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
			};
		const bool directionalRegistered = registerDirectionalReplacement(
			cs::engine::ShaderInjectionTarget::kBsdfLightDeferredDirectional);
		const bool directionalIblRegistered = registerDirectionalReplacement(
			cs::engine::ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl);
		if (!directionalRegistered || !directionalIblRegistered) {
			L->error("Failed to register directional shader replacements.");
		}

		cs::engine::RegisterPreDeferredLightsImpl([] {
			ScreenSpaceShadows::GetSingleton()->OnPreDeferredLights();
		});
		cs::engine::RegisterPostDeferredLightsImpl([] {
			ScreenSpaceShadows::GetSingleton()->OnPostDeferredLights();
		});
		_started.store(true, std::memory_order_release);
		L->info("Registered deferred-lights callbacks (enabled={}).", _settings.enabled);
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

	void ScreenSpaceShadows::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_started.load(std::memory_order_acquire) || !a_device) return;
		EnsureResources();
	}

	bool ScreenSpaceShadows::IsShadowMaskReady()
	{
		return _started.load(std::memory_order_acquire) && EnsureResources();
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

			auto* state = cs::engine::GetGraphicsState();
			if (!state || state->screenWidth == 0 || state->screenHeight == 0) {
				throw std::runtime_error("graphics state has no screen dimensions");
			}
			CreateMaskTexture(state->screenWidth, state->screenHeight);
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
		if (!_started.load(std::memory_order_acquire)) {
			return;
		}

		_dispatchedLastFrame.store(0, std::memory_order_relaxed);
		_maskBoundLastFrame.store(false, std::memory_order_relaxed);
		if (!EnsureResources()) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context || !_maskTexture) {
			return;
		}

		const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->ClearUnorderedAccessViewFloat(_maskTexture->uav.get(), white);

		auto* state = cs::engine::GetGraphicsState();
		if (!state || state->screenWidth == 0 || state->screenHeight == 0) {
			return;
		}
		if (state->screenWidth != _allocWidth || state->screenHeight != _allocHeight) {
			try {
				CreateMaskTexture(state->screenWidth, state->screenHeight);
				context->ClearUnorderedAccessViewFloat(_maskTexture->uav.get(), white);
			} catch (const std::exception& e) {
				L->error("Mask resize failed: {}", e.what());
				return;
			} catch (...) {
				L->error("Mask resize failed.");
				return;
			}
		}

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

						// Sun projection: negate TryGetSunDirectionWS travel dir; use WorldRootCamera::worldToCam, not degenerate per-pass camViewData; column-vector matrix, so transpose for XMVector4Transform.
						auto* sceneCamera = RE::Main::WorldRootCamera();
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

						// Telemetry cache: LightCoordinate_Shader[0..1]=screen-space sun; lightProj[3]=clip.w=dot(toward-sun, view-forward); pump/Ctrl+F12 reads atomics.
						_sunX.store(sx, std::memory_order_relaxed);
						_sunY.store(sy, std::memory_order_relaxed);
						_sunZ.store(sz, std::memory_order_relaxed);
						_lightX.store(dispatchList.LightCoordinate_Shader[0], std::memory_order_relaxed);
						_lightY.store(dispatchList.LightCoordinate_Shader[1], std::memory_order_relaxed);
						_clipW.store(lightProj[3], std::memory_order_relaxed);

						// Dispatch all sun orientations: upstream has no behind-camera gate and trusts Bend w=+/-1 front/behind march; white-clear keeps degenerate frames "no shadow".
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

	void ScreenSpaceShadows::BindShadowMask(ID3D11DeviceContext* a_context)
	{
		// Bind whenever resources exist; disabled still reads the white no-op mask, keeping compiled-in t6 safe.
		if (!a_context ||
			!_resourcesReady.load(std::memory_order_acquire) ||
			!_maskTexture) {
			return;
		}

		// Bind t6 only when engine left it null; ambient/IBL binds g_tAmbientProbeA there, so non-null means not directional sun.
		ID3D11ShaderResourceView* current = nullptr;
		a_context->PSGetShaderResources(kMaskPSSlot, 1, &current);
		if (current) {
			current->Release();
			return;
		}

		auto* srv = _maskTexture->srv.get();
		a_context->PSSetShaderResources(kMaskPSSlot, 1, &srv);
		_maskBound.store(true, std::memory_order_relaxed);
		_maskBoundLastFrame.store(true, std::memory_order_relaxed);
	}

	void ScreenSpaceShadows::OnPostDeferredLights()
	{
		// Restore the engine's expected NULL at t6 so its dirty-state tracking doesn't diverge.
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
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(kMaskPSSlot, 1, &nullSRV);
	}

	void ScreenSpaceShadows::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("resources_ready", _resourcesReady.load(std::memory_order_acquire))
			.Field("mask_bound", _maskBoundLastFrame.load(std::memory_order_relaxed))
			.Field("dispatches", static_cast<std::int64_t>(_dispatchedLastFrame.load(std::memory_order_relaxed)))
			.Dimensions("mask", _allocWidth, _allocHeight)
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

		// Debug mask preview: raw R8 coverage/placement in-game, bright=lit/dark=shadowed, no RenderDoc needed.
		static bool s_showMaskPreview = false;
		ImGui::Checkbox("Show shadow-mask preview (debug)", &s_showMaskPreview);
		if (s_showMaskPreview) {
			if (_maskTexture && _maskTexture->srv && _allocWidth > 0 && _allocHeight > 0) {
				const float aspect = static_cast<float>(_allocWidth) / static_cast<float>(_allocHeight);
				const float previewWidth = 480.0f;
				const float previewHeight = previewWidth / aspect;
				ImGui::TextDisabled("Mask %ux%u (bright = lit, dark = shadowed)", _allocWidth, _allocHeight);
				ImGui::Image(reinterpret_cast<ImTextureID>(_maskTexture->srv.get()),
					ImVec2(previewWidth, previewHeight));
			} else {
				ImGui::TextDisabled("Mask not allocated.");
			}
		}
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
