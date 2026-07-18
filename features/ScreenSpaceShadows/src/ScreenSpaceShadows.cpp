#include "ScreenSpaceShadows.h"

#include <DirectXMath.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/S/Sky.h>
#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Settings/FeatureConfig.h"
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

		constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceShadows.toml";
		constexpr const wchar_t* kRaymarchPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceShadows\\Shaders\\RaymarchCS.hlsl";

		// FO4 uses standard non-linear depth, near=0/far=1 (confirmed by the working DOF Linearize in Imagespace/DepthCoCCS.hlsl). Border color + FarDepthValue must match. If a RenderDoc depth histogram of DepthStencilTarget::kMain shows near objects at 1.0 (reversed-Z), flip to Far=0/Near=1 and set the sampler border to 0.
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
					"shadow_contrast", "number", a_error)
				|| !AcceptSetting(feature_config::ReadFloat(*settingsTable, "max_shadow_length_percent", a_candidate.maxShadowLengthPercent, 0.5f, 15.0f),
					"max_shadow_length_percent", "number", a_error)) {
				return false;
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
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		auto& settings = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("surface_thickness", _settings.surfaceThickness);
		settings.insert_or_assign("bilinear_threshold", _settings.bilinearThreshold);
		settings.insert_or_assign("shadow_contrast", _settings.shadowContrast);
		settings.insert_or_assign("max_shadow_length_percent", _settings.maxShadowLengthPercent);

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(), ec);
		std::ofstream out(kConfigPath);
		if (out) {
			out << table;
		}
	}

	void ScreenSpaceShadows::Load()
	{
		cs::engine::RegisterPreDeferredLightsImpl([] {
			ScreenSpaceShadows::GetSingleton()->OnPreDeferredLights();
		});
		// Bind the mask right before the sun draw (after the engine flushes/nulls PS SRVs), and
		// unbind it when the deferred-lighting phase ends.
		cs::engine::RegisterPreSunLightDraw([] {
			ScreenSpaceShadows::GetSingleton()->OnPreSunLightDraw();
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
		if (_settings.enabled) EnsureResources();
	}

	bool ScreenSpaceShadows::IsShadowMaskReady()
	{
		return _started.load(std::memory_order_acquire) && _settings.enabled && EnsureResources();
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
		constexpr std::uint32_t kMinSampleCount = 8;
		constexpr std::uint32_t kMaxSampleCount = 512;
		auto* state = cs::engine::GetGraphicsState();
		auto* rtm = cs::engine::GetRenderTargetManager();
		if (!state || !rtm) {
			return kMinSampleCount;
		}

		const float height = static_cast<float>(state->screenHeight) * cs::engine::dynres::GetHeightRatio(rtm);
		// SAMPLE_COUNT is the max march reach in screen pixels; derive it from a fraction of render height.
		const float reachPixels = (_settings.maxShadowLengthPercent / 100.0f) * height;
		auto scaled = static_cast<std::uint32_t>(std::round(reachPixels));
		scaled = ((scaled + 7u) / 8u) * 8u;  // quantize to 8 to avoid DRS-oscillation recompiles
		return std::clamp(scaled, kMinSampleCount, kMaxSampleCount);
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

		// The SCREEN_SPACE_SHADOWS define is compiled into the directional shaders at startup, so once
		// active they always sample the mask at t6. Keep the mask allocated + white-cleared whenever it
		// exists (even when runtime-disabled) so the t6 multiply stays a no-op; otherwise a disabled
		// toggle leaves t6 unbound (reads 0) and blacks out the sun. Only the raymarch dispatch below is
		// gated on the enabled toggle.
		if (_settings.enabled) {
			if (!EnsureResources()) {
				return;
			}
		} else if (!_resourcesReady.load(std::memory_order_acquire)) {
			return;
		}
		_dispatchedLastFrame.store(0, std::memory_order_relaxed);

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
					const float widthRatio = cs::engine::dynres::GetWidthRatio(rtm);
					const float heightRatio = cs::engine::dynres::GetHeightRatio(rtm);
					const float renderWidth = static_cast<float>(state->screenWidth) * widthRatio;
					const float renderHeight = static_cast<float>(state->screenHeight) * heightRatio;
					int viewportSize[2] = {
						static_cast<int>(renderWidth),
						static_cast<int>(renderHeight)
					};

					if (viewportSize[0] > 0 && viewportSize[1] > 0) {
						cs::engine::ComputeOMScope scope(context);

						// TryGetSunDirectionWS returns the light-travel direction (cached in the sun node's
						// world.rotate row 0); negate for the toward-sun vector. Source world->clip from the
						// persistent scene camera (camViewData is per-pass scratch the fullscreen deferred pass
						// overwrites). NiCamera::worldToCam is column-vector (clip = M * v), so transform against
						// its transpose; this yields clip.w = dot(toward-sun, view-forward), a proper directional
						// w, instead of the camera world position leaking in and pinning the sun to screen center.
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

						// Debug telemetry (throttled ~2/s): sun projection + dispatch state to sanity-check SSS
						// from the log. Enable via [logging].channels."cs.feature.screenspaceshadows" = "debug".
						// Gated on should_log first so it costs nothing (no clock/format) at the default level.
						if (L->should_log(spdlog::level::debug)) {
							CS_LOG_EVERY_MS(L, 500, spdlog::level::debug,
								"SSS: sunWS=({:.4f},{:.4f},{:.4f}) lightCoord=({:.1f},{:.1f},{:.5f},{:.1f}) "
								"clipW={:.4f} vp={}x{} dispatches={}",
								sx, sy, sz,
								dispatchList.LightCoordinate_Shader[0], dispatchList.LightCoordinate_Shader[1],
								dispatchList.LightCoordinate_Shader[2], dispatchList.LightCoordinate_Shader[3],
								lightProj[3], viewportSize[0], viewportSize[1], dispatchList.DispatchCount);
						}

						// Dispatch for all sun orientations (matches upstream, which ships no behind-camera
						// gate and trusts Bend's own w=+/-1 front/behind march machinery). The mask is
						// white-cleared each frame, so a degenerate frame still reads "no shadow".
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

	void ScreenSpaceShadows::OnPreSunLightDraw()
	{
		if (!_started.load(std::memory_order_acquire)) {
			return;
		}
		// Bind whenever resources exist, regardless of the enabled toggle: when disabled the mask is
		// white (no-op multiply), keeping the compiled-in t6 sample safe. See OnPreDeferredLights.
		if (!_resourcesReady.load(std::memory_order_acquire) || !_maskTexture) {
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

		// Bind t6 only when the engine left it null: the ambient/IBL pass binds g_tAmbientProbeA there,
		// so a non-null t6 means this isn't the directional sun draw and we skip it.
		ID3D11ShaderResourceView* current = nullptr;
		context->PSGetShaderResources(kMaskPSSlot, 1, &current);
		if (current) {
			current->Release();
			return;
		}

		auto* srv = _maskTexture->srv.get();
		context->PSSetShaderResources(kMaskPSSlot, 1, &srv);
		_maskBound.store(true, std::memory_order_relaxed);
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

	void ScreenSpaceShadows::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);
		changed |= ImGui::SliderFloat("Surface thickness", &_settings.surfaceThickness, 0.005f, 0.05f);
		changed |= ImGui::SliderFloat("Bilinear threshold", &_settings.bilinearThreshold, 0.02f, 1.0f);
		changed |= ImGui::SliderFloat("Shadow contrast", &_settings.shadowContrast, 0.0f, 4.0f);

		changed |= ImGui::SliderFloat("Max shadow length", &_settings.maxShadowLengthPercent, 0.5f, 15.0f, "%.1f%% of screen height");
		if (changed) {
			SaveSettings();
		}

		ImGui::TextDisabled(
			"Resources: %s | wave dispatches last frame: %u",
			_resourcesReady.load(std::memory_order_acquire) ? "ready" : "not ready",
			_dispatchedLastFrame.load(std::memory_order_relaxed));

		// Debug: preview the raw R8 shadow mask so its coverage/placement is visible in-game
		// (bright = lit, dark = shadowed). Lets us judge SSS without a RenderDoc capture.
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
