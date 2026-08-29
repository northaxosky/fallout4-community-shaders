#include "ScreenSpaceGI.h"

#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
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
#include "Render/ShaderVariantRuntimeResolver.h"
#include "Settings/FeatureConfig.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenspacegi");

		constexpr const wchar_t* kDecodePath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\decode.cs.hlsl";
		constexpr const wchar_t* kPrefilterPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\prefilterDepths.cs.hlsl";
		constexpr const wchar_t* kPrefilterRadiancePath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\prefilterRadiance.cs.hlsl";
		constexpr const wchar_t* kPrefilterNormalPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\prefilterNormal.cs.hlsl";
		constexpr const wchar_t* kRadianceDisoccPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\radianceDisocc.cs.hlsl";
		constexpr const wchar_t* kAOPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\gi.cs.hlsl";
		constexpr const wchar_t* kDenoisePath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\denoise.cs.hlsl";

		// occlusion and bounce both read as "no contribution" at zero
		constexpr std::array<float, 4> kOpenIdentity{ 0.0f, 0.0f, 0.0f, 0.0f };

		// A single-frame jump past these is a cut, not animation.
		constexpr float kTeleportDistance = 512.0f;
		constexpr float kMinFrameAxisDot = 0.7071f;
		constexpr float kProjectionTolerance = 0.15f;

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

		bool IsFullResolutionHDR(
			ID3D11ShaderResourceView* a_srv,
			std::uint32_t a_width,
			std::uint32_t a_height,
			D3D11_TEXTURE2D_DESC& a_desc)
		{
			if (!a_srv) {
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			a_srv->GetDesc(&srvDesc);
			if (srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				srvDesc.Format != DXGI_FORMAT_R11G11B10_FLOAT) {
				return false;
			}

			winrt::com_ptr<ID3D11Resource> resource;
			a_srv->GetResource(resource.put());
			auto texture = resource.try_as<ID3D11Texture2D>();
			if (!texture) {
				return false;
			}

			texture->GetDesc(&a_desc);
			return a_desc.Width == a_width &&
				a_desc.Height == a_height &&
				a_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
				a_desc.ArraySize == 1 &&
				a_desc.SampleDesc.Count == 1;
		}

		bool IsFullResolutionMotion(
			ID3D11ShaderResourceView* a_srv,
			std::uint32_t a_width,
			std::uint32_t a_height)
		{
			if (!a_srv) {
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			a_srv->GetDesc(&srvDesc);
			if (srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				srvDesc.Format != DXGI_FORMAT_R16G16_FLOAT) {
				return false;
			}

			winrt::com_ptr<ID3D11Resource> resource;
			a_srv->GetResource(resource.put());
			auto texture = resource.try_as<ID3D11Texture2D>();
			if (!texture) {
				return false;
			}

			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			return desc.Width == a_width &&
				desc.Height == a_height &&
				desc.Format == DXGI_FORMAT_R16G16_FLOAT &&
				desc.ArraySize == 1 &&
				desc.SampleDesc.Count == 1;
		}

		winrt::com_ptr<ID3D11Resource> ResourceIdentity(ID3D11ShaderResourceView* a_srv)
		{
			if (!a_srv) {
				return {};
			}

			winrt::com_ptr<ID3D11Resource> resource;
			a_srv->GetResource(resource.put());
			return resource;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			ScreenSpaceGI::Settings& a_candidate,
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

			if (!AcceptSetting(
					feature_config::ReadBool(*settingsTable, "denoise_enabled", a_candidate.denoiseEnabled),
					"denoise_enabled", "boolean", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "denoise_radius", a_candidate.denoiseRadius),
					"denoise_radius", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "ao_radius", a_candidate.aoRadius),
					"ao_radius", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "gi_radius", a_candidate.giRadius),
					"gi_radius", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "ao_power", a_candidate.aoPower),
					"ao_power", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(
						*settingsTable, "bounce_strength", a_candidate.bounceStrength, 0.0f, 8.0f),
					"bounce_strength", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "depth_fade_start", a_candidate.depthFadeStart),
					"depth_fade_start", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "depth_fade_end", a_candidate.depthFadeEnd),
					"depth_fade_end", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled),
					"enabled", "boolean", a_error) ||
				!AcceptSetting(
					feature_config::ReadBool(
						*settingsTable, "enable_temporal_denoiser", a_candidate.enableTemporalDenoiser),
					"enable_temporal_denoiser", "boolean", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(
						*settingsTable, "depth_disocclusion", a_candidate.depthDisocclusion, 0.0f, 0.2f),
					"depth_disocclusion", "number", a_error)) {
				return false;
			}

			auto readInteger = [&](std::string_view a_key, int& a_value, std::int64_t a_min, std::int64_t a_max) {
				auto value = static_cast<std::int64_t>(a_value);
				const auto status = feature_config::ReadSignedInteger(
					*settingsTable, a_key, value, a_min, a_max);
				if (!AcceptSetting(status, a_key, "integer", a_error)) {
					return false;
				}
				if (status == feature_config::ScalarReadStatus::kValid) {
					a_value = static_cast<int>(value);
				}
				return true;
			};

			return readInteger("num_slices", a_candidate.numSlices, 1, 64) &&
				readInteger("num_steps", a_candidate.numSteps, 1, 64) &&
				readInteger("max_accum_frames", a_candidate.maxAccumFrames, 1, 255);
		}

		std::unique_ptr<cs::buffer::Texture2D> CreateTexture(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format,
			std::uint32_t a_mipLevels = 1,
			bool a_createMipZeroUAV = true)
		{
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = a_width;
			textureDesc.Height = a_height;
			textureDesc.MipLevels = a_mipLevels;
			textureDesc.ArraySize = 1;
			textureDesc.Format = a_format;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			auto texture = std::make_unique<cs::buffer::Texture2D>(textureDesc);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = textureDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = a_mipLevels;
			texture->CreateSRV(srvDesc);

			if (a_createMipZeroUAV) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
				uavDesc.Format = textureDesc.Format;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				uavDesc.Texture2D.MipSlice = 0;
				texture->CreateUAV(uavDesc);
			}

			return texture;
		}

		void CreateMipUAVs(
			ID3D11Device* a_device,
			const cs::buffer::Texture2D& a_texture,
			DXGI_FORMAT a_format,
			std::span<winrt::com_ptr<ID3D11UnorderedAccessView>> a_out)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = a_format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			for (std::size_t mip = 0; mip < a_out.size(); ++mip) {
				uavDesc.Texture2D.MipSlice = static_cast<UINT>(mip);
				DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(
					a_texture.resource.get(), &uavDesc, a_out[mip].put()));
			}
		}

		ID3D11ShaderResourceView* SRVOf(const std::unique_ptr<cs::buffer::Texture2D>& a_texture)
		{
			return a_texture ? a_texture->srv.get() : nullptr;
		}

		void ClearToIdentity(
			ID3D11DeviceContext* a_context,
			const std::unique_ptr<cs::buffer::Texture2D>& a_texture)
		{
			if (a_texture && a_texture->uav) {
				a_context->ClearUnorderedAccessViewFloat(a_texture->uav.get(), kOpenIdentity.data());
			}
		}

		std::uint32_t DispatchGroups(int a_extent, std::uint32_t a_groupSize)
		{
			return (static_cast<std::uint32_t>(a_extent) + a_groupSize - 1u) / a_groupSize;
		}

		std::uint32_t ActiveExtent(std::uint32_t a_extent, float a_ratio)
		{
			if (!std::isfinite(a_ratio) || a_ratio <= 0.0f) {
				return 0;
			}
			return std::min(
				a_extent,
				static_cast<std::uint32_t>(static_cast<double>(a_extent) * a_ratio));
		}

		// Binds exactly what a pass needs, dispatches, then narrow-unbinds the same slots.
		class ComputePass
		{
		public:
			explicit ComputePass(ID3D11DeviceContext* a_context) noexcept :
				_context(a_context)
			{
				// Tiled lighting can leave its B buffers in the owned low slots.
				static constexpr ID3D11ShaderResourceView* nullSRVs[8]{};
				static constexpr ID3D11UnorderedAccessView* nullUAVs[8]{};
				_context->CSSetShaderResources(0, 8, nullSRVs);
				_context->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);
			}

			void Dispatch(
				ID3D11ComputeShader* a_shader,
				std::span<ID3D11ShaderResourceView* const> a_srvs,
				std::span<ID3D11UnorderedAccessView* const> a_uavs,
				ID3D11Buffer* a_constants,
				ID3D11SamplerState* a_sampler,
				std::uint32_t a_groupsX,
				std::uint32_t a_groupsY)
			{
				const auto srvCount = static_cast<UINT>(a_srvs.size());
				const auto uavCount = static_cast<UINT>(a_uavs.size());
				_context->CSSetShaderResources(0, srvCount, a_srvs.data());
				_context->CSSetUnorderedAccessViews(0, uavCount, a_uavs.data(), nullptr);
				_context->CSSetConstantBuffers(0, 1, &a_constants);
				_context->CSSetSamplers(0, 1, &a_sampler);
				_context->CSSetShader(a_shader, nullptr, 0);
				_context->Dispatch(a_groupsX, a_groupsY, 1);

				static constexpr ID3D11ShaderResourceView* nullSRVs[8]{};
				static constexpr ID3D11UnorderedAccessView* nullUAVs[8]{};
				ID3D11Buffer* nullConstants = nullptr;
				ID3D11SamplerState* nullSampler = nullptr;
				_context->CSSetShaderResources(0, srvCount, nullSRVs);
				_context->CSSetUnorderedAccessViews(0, uavCount, nullUAVs, nullptr);
				_context->CSSetConstantBuffers(0, 1, &nullConstants);
				_context->CSSetSamplers(0, 1, &nullSampler);
				_context->CSSetShader(nullptr, nullptr, 0);
			}

		private:
			ID3D11DeviceContext* _context;
		};
	}

	ScreenSpaceGI* ScreenSpaceGI::GetSingleton()
	{
		static ScreenSpaceGI instance;
		return &instance;
	}

	std::span<const FeatureDebugView> ScreenSpaceGI::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "occlusion",
				.label = "Occlusion preview",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const ScreenSpaceGI&>(a_feature)
						.GetOcclusionDebugTexture();
				}
			}
		};
		return views;
	}

	void ScreenSpaceGI::SetDebugView(std::string_view a_view) noexcept
	{
		_debugPreviewEnabled.store(
			a_view == "occlusion",
			std::memory_order_release);
	}

	FeatureDebugTexture ScreenSpaceGI::GetOcclusionDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Buffer not allocated."
		};
		const auto& source =
			_aoDenoisedLastFrame.load(std::memory_order_relaxed) ?
				_aoDenoisedTex :
				_aoRawTex;
		if (!_debugPreviewEnabled.load(std::memory_order_acquire)
			|| !source
			|| !source->srv
			|| _allocW == 0
			|| _allocH == 0) {
			return texture;
		}
		texture.texture = source->srv.get();
		texture.width = _allocW;
		texture.height = _allocH;
		texture.caption = std::format(
			"Occlusion (bright = occluded) {}x{}",
			_allocW,
			_allocH);
		return texture;
	}

	bool ScreenSpaceGI::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		return true;
	}

	void ScreenSpaceGI::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("denoise_enabled", _settings.denoiseEnabled);
		settings.insert_or_assign("denoise_radius", _settings.denoiseRadius);
		settings.insert_or_assign("ao_radius", _settings.aoRadius);
		settings.insert_or_assign("gi_radius", _settings.giRadius);
		settings.insert_or_assign("ao_power", _settings.aoPower);
		settings.insert_or_assign("bounce_strength", _settings.bounceStrength);
		settings.insert_or_assign("depth_fade_start", _settings.depthFadeStart);
		settings.insert_or_assign("depth_fade_end", _settings.depthFadeEnd);
		settings.insert_or_assign("num_slices", _settings.numSlices);
		settings.insert_or_assign("num_steps", _settings.numSteps);
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("enable_temporal_denoiser", _settings.enableTemporalDenoiser);
		settings.insert_or_assign("depth_disocclusion", _settings.depthDisocclusion);
		settings.insert_or_assign("max_accum_frames", _settings.maxAccumFrames);

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settings); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void ScreenSpaceGI::Load()
	{
		std::vector<cs::engine::ShaderSlotClaim> slotClaims;
		slotClaims.reserve(kCompositionPSSlotCount);
		for (std::uint32_t offset = 0; offset < kCompositionPSSlotCount; ++offset) {
			slotClaims.push_back({
				.stage = cs::engine::ShaderStage::kPixel,
				.resourceType = cs::engine::ShaderResourceType::kShaderResource,
				.slot = kCompositionPSSlot + offset
			});
		}

		const bool registered = cs::engine::RegisterReplacement({
			.targetId = cs::engine::ShaderInjectionTarget::kBsdfComposite,
			.contributor = "ScreenSpaceGI",
			.defines = {
				{
					cs::engine::shader_injection_defines::kScreenSpaceGi,
					"1"
				}
			},
			.bind = [this](ID3D11DeviceContext* a_context) {
				BindComposition(a_context);
			},
			.slotClaims = std::move(slotClaims)
		});
		if (!registered) {
			FailLoad(
				"ScreenSpaceGI composes through the reconstructed BSDFComposite shader; "
				"registering that replacement failed, so there is no delivery path");
			return;
		}

		_injectionRegistered.store(true, std::memory_order_release);
		const bool compositionScopeRegistered =
			cs::engine::RegisterPreDeferredComposite([] {
				ScreenSpaceGI::GetSingleton()->SaveCompositionBindings();
			}, cs::engine::HookPriority::Early)
			&& cs::engine::RegisterPostDeferredComposite([] {
				ScreenSpaceGI::GetSingleton()->RestoreCompositionBindings();
			}, cs::engine::HookPriority::Late);
		if (!compositionScopeRegistered) {
			FailLoad(
				"ScreenSpaceGI needs a paired composite save and restore to hand its "
				"bindings back to the engine; registering that pair failed");
			return;
		}
		// Deferred lighting has written the radiance source by this anchor.
		cs::engine::RegisterPostDeferredLightsImpl([] {
			ScreenSpaceGI::GetSingleton()->OnPostDeferredLights();
		});

		// Seed the transition detectors so the first frame is not a spurious re-enable.
		_lastEnabled = _settings.enabled;
		_lastTemporalEnabled = _settings.enableTemporalDenoiser;
		_started.store(true, std::memory_order_release);
		L->info(
			"Registered composite injection and post-deferred-lights callback (enabled={}).",
			_settings.enabled);
	}

	void ScreenSpaceGI::OnDataLoaded()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->RegisterSink<RE::MenuOpenCloseEvent>(this);
		}
	}

	RE::BSEventNotifyControl ScreenSpaceGI::ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		// The event thread only queues; the render thread owns history and GPU state.
		if (a_event.menuName == RE::LoadingMenu::MENU_NAME && !a_event.opening) {
			_queuedHistoryReset.store(true, std::memory_order_release);
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void ScreenSpaceGI::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_started.load(std::memory_order_acquire) || !a_device) return;

		auto compile = [](
			winrt::com_ptr<ID3D11ComputeShader>& a_target,
			const wchar_t* a_path,
			const std::vector<std::pair<const char*, const char*>>& a_defines,
			const char* a_label) {
			a_target.attach(reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(a_path, a_defines, "cs_5_0")));
			if (!a_target) {
				L->warn("Failed to compile XeGTAO {} shader.", a_label);
			}
		};

		compile(_decodeCS, kDecodePath, {}, "decode");
		compile(_prefilterCS, kPrefilterPath, { { "LINEAR_FILTER", "1" } }, "depth prefilter");
		compile(_prefilterRadianceCS, kPrefilterRadiancePath, {}, "radiance prefilter");
		compile(_prefilterNormalCS, kPrefilterNormalPath, {}, "normal prefilter");
		compile(_radianceDisoccCS, kRadianceDisoccPath, {}, "radiance disocclusion");
		compile(_aoCS, kAOPath, {}, "AO");
		compile(_bounceCS, kAOPath, { { "SSGI_BOUNCE", "1" } }, "bounce");
		compile(_denoiseCS, kDenoisePath, {}, "denoise");
		compile(_bounceDenoiseCS, kDenoisePath, { { "SSGI_BOUNCE", "1" } }, "bounce denoise");

		if (!_pointClampSampler) {
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxAnisotropy = 1;
			samplerDesc.MinLOD = 0.0f;
			samplerDesc.MaxLOD = FLT_MAX;
			DX::ThrowIfFailed(cs::util::GetD3DDevice()->CreateSamplerState(&samplerDesc, _pointClampSampler.put()));
		}

		// the injected variant is runtime-toggled, so allocate regardless of the setting
		EnsureResources();
	}

	bool ScreenSpaceGI::IsGeneratorReady() const noexcept
	{
		return _decodeCS && _prefilterCS && _aoCS &&
			_linearDepthTex && _workingDepthTex && _viewNormalTex &&
			_aoRawTex && _aoDenoisedTex &&
			_noiseSRV && _pointClampSampler && _xegtaoCB && _decodeCB &&
			_workingDepthMipUAVs[kMipCount - 1] && _viewNormalMipUAVs[kMipCount - 1] &&
			_viewNormalMip0SRV;
	}

	bool ScreenSpaceGI::IsTemporalReady() const noexcept
	{
		return _bounceCS && _radianceDisoccCS && _prefilterRadianceCS && _prefilterNormalCS &&
			_radianceTempTex && _radianceTex && _radianceMipUAVs[kMipCount - 1] &&
			_bounceSHRawTex && _bounceCoCgRawTex && _accumBlurTex &&
			_bounceSHTex[0] && _bounceSHTex[1] &&
			_bounceCoCgTex[0] && _bounceCoCgTex[1] &&
			_accumTex[0] && _accumTex[1] &&
			_prevGeoTex[0] && _prevGeoTex[1];
	}

	cs::ScreenSpaceGIFeatureData ScreenSpaceGI::GetCommonBufferData()
	{
		if (!_settings.enabled ||
			!_injectionRegistered.load(std::memory_order_acquire) ||
			!_resourcesReady.load(std::memory_order_acquire) ||
			!_aoProducedLastFrame.load(std::memory_order_relaxed) ||
			!IsGeneratorReady() ||
			!cs::engine::GetRenderTargetSRV(
				cs::engine::RenderTarget::kGbufferAlbedo)) {
			return {};
		}
		return {
			.EnableScreenSpaceGI = 1,
			.pad0 = 0,
			.AoPower = _settings.aoPower,
			.BounceStrength = _settings.bounceStrength
		};
	}

	bool ScreenSpaceGI::EnsureResources()
	{
		if (_resourceInitFailed.load(std::memory_order_acquire)) {
			return false;
		}
		if (!cs::util::GetD3DDevice()) {
			return false;
		}

		const bool resourcesReady = _resourcesReady.load(std::memory_order_acquire);
		const bool hadResources = _aoRawTex != nullptr;

		// an engine that has not published dimensions or a context yet is not a failure
		auto* state = cs::engine::GetGraphicsState();
		if (!state || state->screenWidth == 0 || state->screenHeight == 0) {
			return resourcesReady;
		}
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ?
			reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			nullptr;
		if (!context) {
			return resourcesReady;
		}

		// Full-resolution allocation avoids dynamic-resolution churn.
		const std::uint32_t width = state->screenWidth;
		const std::uint32_t height = state->screenHeight;
		if (resourcesReady && width == _allocW && height == _allocH) {
			return true;
		}

		try {
			if (hadResources) {
				_resourcesReady.store(false, std::memory_order_release);
			}

			auto* device = cs::util::GetD3DDevice();

			auto linearDepthTex = CreateTexture(width, height, DXGI_FORMAT_R32_FLOAT);
			auto workingDepthTex = CreateTexture(width, height, DXGI_FORMAT_R32_FLOAT, kMipCount, false);
			auto viewNormalTex = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, kMipCount, false);
			auto radianceTempTex = CreateTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT);
			auto radianceTex = CreateTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT, kMipCount, false);
			auto aoRawTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
			auto aoDenoisedTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
			auto bounceSHRawTex = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
			auto bounceCoCgRawTex = CreateTexture(width, height, DXGI_FORMAT_R16G16_FLOAT);
			auto accumBlurTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
			std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> bounceSHTex;
			std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> bounceCoCgTex;
			std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> accumTex;
			std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> prevGeoTex;
			for (std::size_t index = 0; index < 2; ++index) {
				bounceSHTex[index] = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
				bounceCoCgTex[index] = CreateTexture(width, height, DXGI_FORMAT_R16G16_FLOAT);
				accumTex[index] = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
				prevGeoTex[index] = CreateTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT);
			}
			auto xegtaoCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<XeGTAOCB>());
			auto decodeCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<DecodeCB>());

			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> workingDepthMipUAVs;
			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> viewNormalMipUAVs;
			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> radianceMipUAVs;
			CreateMipUAVs(device, *workingDepthTex, DXGI_FORMAT_R32_FLOAT, workingDepthMipUAVs);
			CreateMipUAVs(device, *viewNormalTex, DXGI_FORMAT_R16G16B16A16_FLOAT, viewNormalMipUAVs);
			CreateMipUAVs(device, *radianceTex, DXGI_FORMAT_R11G11B10_FLOAT, radianceMipUAVs);

			// Mip 0 alone, so the prefilter never aliases its own output subresources.
			winrt::com_ptr<ID3D11ShaderResourceView> viewNormalMip0SRV;
			D3D11_SHADER_RESOURCE_VIEW_DESC mip0SRVDesc{};
			mip0SRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			mip0SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			mip0SRVDesc.Texture2D.MostDetailedMip = 0;
			mip0SRVDesc.Texture2D.MipLevels = 1;
			DX::ThrowIfFailed(device->CreateShaderResourceView(
				viewNormalTex->resource.get(), &mip0SRVDesc, viewNormalMip0SRV.put()));

			winrt::com_ptr<ID3D11Texture2D> noiseTex;
			winrt::com_ptr<ID3D11ShaderResourceView> noiseSRV;
			if (!_noiseTex) {
				constexpr std::uint32_t kNoiseWidth = 128;
				constexpr std::uint32_t kNoiseHeight = 8192;
				std::vector<std::uint8_t> noiseData(kNoiseWidth * kNoiseHeight * 2);
				auto hilbertIndex = [](std::uint32_t a_posX, std::uint32_t a_posY) -> std::uint32_t {
					std::uint32_t index = 0u;
					for (std::uint32_t curLevel = 64u / 2u; curLevel > 0u; curLevel /= 2u) {
						const std::uint32_t regionX = (a_posX & curLevel) > 0u ? 1u : 0u;
						const std::uint32_t regionY = (a_posY & curLevel) > 0u ? 1u : 0u;
						index += curLevel * curLevel * ((3u * regionX) ^ regionY);
						if (regionY == 0u) {
							if (regionX == 1u) {
								a_posX = 63u - a_posX;
								a_posY = 63u - a_posY;
							}
							std::swap(a_posX, a_posY);
						}
					}
					return index;
				};
				constexpr double kR2X = 0.75487766624669276005;
				constexpr double kR2Y = 0.569840290998053414;
				for (std::uint32_t t = 0; t < 64u; ++t) {
					for (std::uint32_t yy = 0; yy < 128u; ++yy) {
						for (std::uint32_t x = 0; x < 128u; ++x) {
							const std::uint32_t index = hilbertIndex(x % 64u, yy % 64u) + 288u * t;
							const double nx = std::fmod(0.5 + static_cast<double>(index) * kR2X, 1.0);
							const double ny = std::fmod(0.5 + static_cast<double>(index) * kR2Y, 1.0);
							const std::size_t texel =
								(static_cast<std::size_t>(t) * 128u + yy) * 128u + x;
							noiseData[texel * 2 + 0] =
								static_cast<std::uint8_t>(std::lround(nx * 255.0));
							noiseData[texel * 2 + 1] =
								static_cast<std::uint8_t>(std::lround(ny * 255.0));
						}
					}
				}

				D3D11_TEXTURE2D_DESC noiseDesc{};
				noiseDesc.Width = kNoiseWidth;
				noiseDesc.Height = kNoiseHeight;
				noiseDesc.MipLevels = 1;
				noiseDesc.ArraySize = 1;
				noiseDesc.Format = DXGI_FORMAT_R8G8_UNORM;
				noiseDesc.SampleDesc.Count = 1;
				noiseDesc.Usage = D3D11_USAGE_IMMUTABLE;
				noiseDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

				D3D11_SUBRESOURCE_DATA initialData{};
				initialData.pSysMem = noiseData.data();
				initialData.SysMemPitch = kNoiseWidth * 2;
				DX::ThrowIfFailed(device->CreateTexture2D(&noiseDesc, &initialData, noiseTex.put()));

				D3D11_SHADER_RESOURCE_VIEW_DESC noiseSRVDesc{};
				noiseSRVDesc.Format = DXGI_FORMAT_R8G8_UNORM;
				noiseSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				noiseSRVDesc.Texture2D.MostDetailedMip = 0;
				noiseSRVDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(noiseTex.get(), &noiseSRVDesc, noiseSRV.put()));
			}

			_resourcesReady.store(false, std::memory_order_release);
			_linearDepthTex = std::move(linearDepthTex);
			_workingDepthTex = std::move(workingDepthTex);
			_workingDepthMipUAVs = std::move(workingDepthMipUAVs);
			_viewNormalTex = std::move(viewNormalTex);
			_viewNormalMipUAVs = std::move(viewNormalMipUAVs);
			_viewNormalMip0SRV = std::move(viewNormalMip0SRV);
			_radianceTempTex = std::move(radianceTempTex);
			_radianceTex = std::move(radianceTex);
			_radianceMipUAVs = std::move(radianceMipUAVs);
			_aoRawTex = std::move(aoRawTex);
			_aoDenoisedTex = std::move(aoDenoisedTex);
			_bounceSHRawTex = std::move(bounceSHRawTex);
			_bounceCoCgRawTex = std::move(bounceCoCgRawTex);
			_bounceSHTex = std::move(bounceSHTex);
			_bounceCoCgTex = std::move(bounceCoCgTex);
			_accumTex = std::move(accumTex);
			_prevGeoTex = std::move(prevGeoTex);
			_accumBlurTex = std::move(accumBlurTex);
			_xegtaoCB = std::move(xegtaoCB);
			_decodeCB = std::move(decodeCB);
			if (noiseTex) {
				_noiseTex = std::move(noiseTex);
				_noiseSRV = std::move(noiseSRV);
			}

			// fresh allocations must read as fully open until the generator runs
			_occlusionOutputsDirty = true;
			_bounceOutputsDirty = true;
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			ClearTemporalHistory(context);

			_allocW = width;
			_allocH = height;
			++_generation;
			ResetHistory(hadResources ?
				ssgi::HistoryResetReason::kResize :
				ssgi::HistoryResetReason::kResourceCreate);
			_resourcesReady.store(true, std::memory_order_release);
			L->info("Resources ready ({}x{}, generation {}).", _allocW, _allocH, _generation);
			return true;
		} catch (const std::exception& e) {
			if (hadResources) {
				L->error("Resource resize failed: {}", e.what());
			} else {
				_resourceInitFailed.store(true, std::memory_order_release);
				L->error("Resource creation failed: {}", e.what());
			}
			return false;
		} catch (...) {
			if (hadResources) {
				L->error("Resource resize failed.");
			} else {
				_resourceInitFailed.store(true, std::memory_order_release);
				L->error("Resource creation failed.");
			}
			return false;
		}
	}

	void ScreenSpaceGI::ResetHistory(ssgi::HistoryResetReason a_reason)
	{
		_history.Reset(a_reason);
		_prevCameraValid = false;
		_historyResetCount.store(_history.ResetCount(), std::memory_order_relaxed);
		_lastResetReason.store(static_cast<std::uint32_t>(a_reason), std::memory_order_relaxed);
	}

	void ScreenSpaceGI::ClearOcclusionOutputs(ID3D11DeviceContext* a_context)
	{
		if (!_occlusionOutputsDirty || !a_context) {
			return;
		}
		ClearToIdentity(a_context, _aoRawTex);
		ClearToIdentity(a_context, _aoDenoisedTex);
		_occlusionOutputsDirty = false;
	}

	void ScreenSpaceGI::ClearBounceOutputs(ID3D11DeviceContext* a_context)
	{
		if (!_bounceOutputsDirty || !a_context) {
			return;
		}
		ClearToIdentity(a_context, _bounceSHRawTex);
		ClearToIdentity(a_context, _bounceCoCgRawTex);
		for (std::size_t index = 0; index < 2; ++index) {
			ClearToIdentity(a_context, _bounceSHTex[index]);
			ClearToIdentity(a_context, _bounceCoCgTex[index]);
		}
		_bounceOutputsDirty = false;
	}

	void ScreenSpaceGI::ClearTemporalHistory(ID3D11DeviceContext* a_context)
	{
		if (!a_context) {
			return;
		}
		for (std::size_t index = 0; index < 2; ++index) {
			ClearToIdentity(a_context, _bounceSHTex[index]);
			ClearToIdentity(a_context, _bounceCoCgTex[index]);
			ClearToIdentity(a_context, _accumTex[index]);
			ClearToIdentity(a_context, _prevGeoTex[index]);
		}
	}

	void ScreenSpaceGI::OnPostDeferredLights()
	{
		if (!_started.load(std::memory_order_acquire)) {
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

		auto* state = cs::engine::GetGraphicsState();
		if (state) {
			// The anchor can fire more than once per engine frame; only the first dispatches.
			if (_lastCallbackFrameValid && state->frameCount == _lastCallbackFrame) {
				_repeatCallbacks.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			_lastCallbackFrame = state->frameCount;
			_lastCallbackFrameValid = true;
		}

		_aoProducedLastFrame.store(false, std::memory_order_relaxed);
		_aoDenoisedLastFrame.store(false, std::memory_order_relaxed);
		_bounceProducedLastFrame.store(false, std::memory_order_relaxed);
		_bounceDenoisedLastFrame.store(false, std::memory_order_relaxed);
		_radianceAvailableLastFrame.store(false, std::memory_order_relaxed);
		_albedoBoundLastFrame.store(false, std::memory_order_relaxed);
		_historyValidLastFrame.store(false, std::memory_order_relaxed);
		_motionAvailableLastFrame.store(false, std::memory_order_relaxed);
		_tiledBAvailable.store(false, std::memory_order_relaxed);
		_compositionBindsLastFrame.store(0, std::memory_order_relaxed);
		_temporalDispatchesLastFrame.store(0, std::memory_order_relaxed);
		_radianceSourceCount.store(0, std::memory_order_relaxed);

		if (_queuedHistoryReset.exchange(false, std::memory_order_acq_rel)) {
			ResetHistory(ssgi::HistoryResetReason::kLoadingScreenClosed);
		}
		if (_settings.enabled && !_lastEnabled) {
			ResetHistory(ssgi::HistoryResetReason::kFeatureReEnabled);
		}
		_lastEnabled = _settings.enabled;
		const bool temporalEnabled = _settings.enableTemporalDenoiser;
		if (temporalEnabled != _lastTemporalEnabled) {
			ResetHistory(ssgi::HistoryResetReason::kTemporalSettingChanged);
		}
		_lastTemporalEnabled = temporalEnabled;

		if (!_settings.enabled || !EnsureResources()) {
			if (_history.Valid()) {
				ResetHistory(ssgi::HistoryResetReason::kMissingInputs);
			}
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			return;
		}

		auto* rtm = cs::engine::GetRenderTargetManager();
		auto* sceneCamera = cs::engine::GetWorldRootCamera();
		DirectX::XMFLOAT4X4 worldProj{};
		DirectX::XMFLOAT4X4 worldInvProj{};
		DirectX::XMFLOAT4 worldNdcToViewMul{};
		DirectX::XMFLOAT4 worldNdcToViewAdd{};
		auto* depthSRV = cs::engine::GetSceneDepthSRV();
		auto* normalSRV = cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
		if (!state || !rtm || !sceneCamera || !IsGeneratorReady() || !depthSRV || !normalSRV ||
			!cs::engine::TryGetWorldSceneProjection(
				worldProj, worldInvProj, worldNdcToViewMul, worldNdcToViewAdd)) {
			if (_history.Valid()) {
				ResetHistory(ssgi::HistoryResetReason::kMissingInputs);
			}
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			return;
		}

		const std::uint32_t frameW = ActiveExtent(_allocW, rtm->GetDynamicWidthRatio());
		const std::uint32_t frameH = ActiveExtent(_allocH, rtm->GetDynamicHeightRatio());
		if (frameW == 0 || frameH == 0) {
			if (_history.Valid()) {
				ResetHistory(ssgi::HistoryResetReason::kMissingInputs);
			}
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			return;
		}

		D3D11_TEXTURE2D_DESC radianceDesc{};
		auto* radianceSRV = cs::engine::GetRenderTargetSRV(kRadianceSourceA);
		const bool radianceAvailable =
			IsTemporalReady() &&
			IsFullResolutionHDR(radianceSRV, _allocW, _allocH, radianceDesc);
		_radianceAvailableLastFrame.store(radianceAvailable, std::memory_order_relaxed);
		if (!radianceAvailable) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"SSGI bounce unavailable: the radiance source must expose a full-resolution R11G11B10_FLOAT SRV.");
		}

		// The B descriptor is only ever retrieved while the tiled predicate is known and true.
		const auto tiledLighting = cs::engine::QueryTiledLightingEnabled();
		_tiledPredicateAvailable.store(tiledLighting.has_value(), std::memory_order_relaxed);
		_tiledLightingActive.store(tiledLighting.value_or(false), std::memory_order_relaxed);
		ID3D11ShaderResourceView* radianceBSRV = nullptr;
		if (radianceAvailable && tiledLighting.has_value() && *tiledLighting) {
			auto* candidate = cs::engine::GetRenderTargetSRV(kRadianceSourceB);
			D3D11_TEXTURE2D_DESC candidateDesc{};
			if (IsFullResolutionHDR(candidate, radianceDesc.Width, radianceDesc.Height, candidateDesc)) {
				radianceBSRV = candidate;
			} else {
				CS_LOG_ONCE(
					L,
					spdlog::level::warn,
					"SSGI tiled lighting is active but the second radiance buffer does not match the first; using one source.");
			}
		}
		const bool includeSourceB = radianceBSRV != nullptr;
		_tiledBAvailable.store(includeSourceB, std::memory_order_relaxed);
		_radianceSourceCount.store(
			radianceAvailable ? (includeSourceB ? 2u : 1u) : 0u, std::memory_order_relaxed);

		auto* motionSRV = cs::engine::GetRenderTargetSRV(kMotionSource);
		const bool motionAvailable = IsFullResolutionMotion(motionSRV, _allocW, _allocH);
		_motionAvailableLastFrame.store(motionAvailable, std::memory_order_relaxed);

		CameraTransform camera{};
		for (std::size_t row = 0; row < 3; ++row) {
			const auto& entry = sceneCamera->world.rotate.entry[row];
			camera.rows[row * 4 + 0] = entry.x;
			camera.rows[row * 4 + 1] = entry.y;
			camera.rows[row * 4 + 2] = entry.z;
		}
		camera.rows[3] = state->cameraState.posAdjust.x;
		camera.rows[7] = state->cameraState.posAdjust.y;
		camera.rows[11] = state->cameraState.posAdjust.z;
		camera.ndcToViewMul[0] = worldNdcToViewMul.x;
		camera.ndcToViewMul[1] = worldNdcToViewMul.y;
		camera.ndcToViewAdd[0] = worldNdcToViewAdd.x;
		camera.ndcToViewAdd[1] = worldNdcToViewAdd.y;

		const InputIdentity inputs{
			.depth = ResourceIdentity(depthSRV),
			.normal = ResourceIdentity(normalSRV),
			.motion = ResourceIdentity(motionAvailable ? motionSRV : nullptr),
			.sourceA = ResourceIdentity(radianceAvailable ? radianceSRV : nullptr),
			.sourceB = ResourceIdentity(radianceBSRV)
		};
		const std::uint8_t sourceMode =
			(tiledLighting.has_value() ? 1u : 0u) |
			(tiledLighting.value_or(false) ? 2u : 0u) |
			(includeSourceB ? 4u : 0u);
		const bool sourceModeChanged = sourceMode != _lastSourceMode;
		if (sourceModeChanged || !(inputs == _lastInputs)) {
			if (_history.Valid()) {
				ResetHistory(sourceModeChanged ?
					ssgi::HistoryResetReason::kSourceModeChanged :
					ssgi::HistoryResetReason::kInputGenerationChange);
			}
			_lastSourceMode = sourceMode;
			_lastInputs = inputs;
		}
		if (!radianceAvailable && _history.Valid()) {
			ResetHistory(ssgi::HistoryResetReason::kMissingInputs);
		}
		if (!motionAvailable && _history.Valid()) {
			ResetHistory(ssgi::HistoryResetReason::kMissingMotion);
		}
		if (_prevCameraValid && _history.Valid()) {
			const float deltaX = camera.rows[3] - _prevCamera.rows[3];
			const float deltaY = camera.rows[7] - _prevCamera.rows[7];
			const float deltaZ = camera.rows[11] - _prevCamera.rows[11];
			bool discontinuous =
				(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) >
					kTeleportDistance * kTeleportDistance;
			for (std::size_t row = 0; !discontinuous && row < 3; ++row) {
				const std::size_t offset = row * 4;
				const float axisDot =
					_prevCamera.rows[offset + 0] * camera.rows[offset + 0] +
					_prevCamera.rows[offset + 1] * camera.rows[offset + 1] +
					_prevCamera.rows[offset + 2] * camera.rows[offset + 2];
				discontinuous = axisDot < kMinFrameAxisDot;
			}
			for (std::size_t axis = 0; !discontinuous && axis < 2; ++axis) {
				const float currentMul = camera.ndcToViewMul[axis];
				const float previousMul = _prevCamera.ndcToViewMul[axis];
				const float currentAdd = camera.ndcToViewAdd[axis];
				const float previousAdd = _prevCamera.ndcToViewAdd[axis];
				const float mulScale = std::max(
					{ std::abs(currentMul), std::abs(previousMul), 1e-6f });
				const float addScale = std::max(
					{ std::abs(currentAdd), std::abs(previousAdd), 1.0f });
				discontinuous =
					std::abs(currentMul - previousMul) > kProjectionTolerance * mulScale ||
					std::abs(currentAdd - previousAdd) > kProjectionTolerance * addScale;
			}
			if (discontinuous) {
				ResetHistory(ssgi::HistoryResetReason::kCameraDiscontinuity);
			}
		}

		const auto frameIndex = static_cast<std::uint64_t>(state->frameCount);
		const auto historyFrame = _history.Prepare(frameIndex);
		_historyResetCount.store(_history.ResetCount(), std::memory_order_relaxed);
		_lastResetReason.store(
			static_cast<std::uint32_t>(_history.LastResetReason()), std::memory_order_relaxed);
		const bool useHistory = historyFrame.useHistory && temporalEnabled;
		_historyValidLastFrame.store(useHistory, std::memory_order_relaxed);

		try {
			cs::engine::ComputeOMScope scope(context);

			if (_history.ConsumeClearPending()) {
				ClearTemporalHistory(context);
			}

			const float texWidth = static_cast<float>(_allocW);
			const float texHeight = static_cast<float>(_allocH);
			const float frameWidth = static_cast<float>(frameW);
			const float frameHeight = static_cast<float>(frameH);
			const float prevFrameWidth =
				useHistory ? static_cast<float>(_prevFrameW) : frameWidth;
			const float prevFrameHeight =
				useHistory ? static_cast<float>(_prevFrameH) : frameHeight;

			DecodeCB decodeCB{};
			decodeCB.InvProj = worldInvProj;
			decodeCB.RcpFrameDim[0] = 1.0f / frameWidth;
			decodeCB.RcpFrameDim[1] = 1.0f / frameHeight;
			decodeCB.FrameDim[0] = frameWidth;
			decodeCB.FrameDim[1] = frameHeight;
			_decodeCB->Update(decodeCB);

			// one screen-space radius drives the sweep; AO and GI cut it at their own fractions
			const float effectRadius = std::max(
				1.0f, std::max(_settings.aoRadius, _settings.giRadius));

			XeGTAOCB xegtaoCB{};
			xegtaoCB.NDCToViewMul[0] = worldNdcToViewMul.x;
			xegtaoCB.NDCToViewMul[1] = worldNdcToViewMul.y;
			xegtaoCB.NDCToViewMul[2] = worldNdcToViewMul.z;
			xegtaoCB.NDCToViewMul[3] = worldNdcToViewMul.w;
			xegtaoCB.NDCToViewAdd[0] = worldNdcToViewAdd.x;
			xegtaoCB.NDCToViewAdd[1] = worldNdcToViewAdd.y;
			xegtaoCB.NDCToViewAdd[2] = worldNdcToViewAdd.z;
			xegtaoCB.NDCToViewAdd[3] = worldNdcToViewAdd.w;
			xegtaoCB.TexDim[0] = texWidth;
			xegtaoCB.TexDim[1] = texHeight;
			xegtaoCB.RcpTexDim[0] = 1.0f / texWidth;
			xegtaoCB.RcpTexDim[1] = 1.0f / texHeight;
			xegtaoCB.FrameDim[0] = frameWidth;
			xegtaoCB.FrameDim[1] = frameHeight;
			xegtaoCB.RcpFrameDim[0] = 1.0f / frameWidth;
			xegtaoCB.RcpFrameDim[1] = 1.0f / frameHeight;
			xegtaoCB.PrevFrameDim[0] = prevFrameWidth;
			xegtaoCB.PrevFrameDim[1] = prevFrameHeight;
			xegtaoCB.RcpPrevFrameDim[0] = 1.0f / prevFrameWidth;
			xegtaoCB.RcpPrevFrameDim[1] = 1.0f / prevFrameHeight;
			xegtaoCB.FrameIndex = static_cast<std::uint32_t>(state->frameCount);
			xegtaoCB.NumSlices = static_cast<std::uint32_t>(_settings.numSlices);
			xegtaoCB.NumSteps = static_cast<std::uint32_t>(_settings.numSteps);
			xegtaoCB.MinScreenRadius = 3.0f;
			xegtaoCB.AORadius = std::clamp(_settings.aoRadius / effectRadius, 0.0f, 1.0f);
			xegtaoCB.EffectRadius = effectRadius;
			xegtaoCB.Thickness = 32.0f;
			xegtaoCB.GIRadius = std::clamp(_settings.giRadius / effectRadius, 0.0f, 1.0f);
			// Metric-scale fades suppress almost all AO.
			xegtaoCB.DepthFadeRange[0] = _settings.depthFadeStart;
			xegtaoCB.DepthFadeRange[1] = _settings.depthFadeEnd;
			const float depthFadeSpan = _settings.depthFadeEnd - _settings.depthFadeStart;
			xegtaoCB.DepthFadeScaleConst = depthFadeSpan > 1.0f ? 1.0f / depthFadeSpan : 1.0f;
			xegtaoCB.BlurRadius = _settings.denoiseRadius;
			xegtaoCB.DistanceNormalisation = 2.0f;
			xegtaoCB.CenterBeta = 1.0f;
			xegtaoCB.DepthDisocclusion = _settings.depthDisocclusion;
			xegtaoCB.MaxAccumFrames = static_cast<std::uint32_t>(_settings.maxAccumFrames);
			xegtaoCB.TemporalFlags =
				(temporalEnabled ? 1u : 0u) |
				(useHistory ? 2u : 0u) |
				(includeSourceB ? 4u : 0u);
			if (radianceAvailable) {
				xegtaoCB.RadianceScale[0] = frameWidth / static_cast<float>(radianceDesc.Width);
				xegtaoCB.RadianceScale[1] = frameHeight / static_cast<float>(radianceDesc.Height);
			}
			const CameraTransform& previousCamera = useHistory ? _prevCamera : camera;
			std::memcpy(
				xegtaoCB.PrevNDCToViewMul,
				previousCamera.ndcToViewMul,
				sizeof(previousCamera.ndcToViewMul));
			std::memcpy(
				xegtaoCB.PrevNDCToViewAdd,
				previousCamera.ndcToViewAdd,
				sizeof(previousCamera.ndcToViewAdd));
			std::memcpy(xegtaoCB.ViewToWorld, camera.rows, sizeof(camera.rows));
			std::memcpy(
				xegtaoCB.PrevViewToWorld,
				previousCamera.rows,
				sizeof(previousCamera.rows));
			_xegtaoCB->Update(xegtaoCB);

			ComputePass pass(context);
			auto* constants = _xegtaoCB->CB();
			auto* sampler = _pointClampSampler.get();
			const auto groups8X = DispatchGroups(static_cast<int>(frameW), 8u);
			const auto groups8Y = DispatchGroups(static_cast<int>(frameH), 8u);
			const auto groups16X = DispatchGroups(static_cast<int>(frameW), 16u);
			const auto groups16Y = DispatchGroups(static_cast<int>(frameH), 16u);
			const auto readIndex = historyFrame.readIndex;
			const auto writeIndex = historyFrame.writeIndex;
			std::uint32_t temporalDispatches = 0;

			ID3D11ShaderResourceView* decodeSRVs[]{ depthSRV, normalSRV };
			ID3D11UnorderedAccessView* decodeUAVs[]{
				_linearDepthTex->uav.get(),
				_viewNormalMipUAVs[0].get()
			};
			pass.Dispatch(
				_decodeCS.get(), decodeSRVs, decodeUAVs, _decodeCB->CB(), sampler,
				groups8X, groups8Y);

			if (radianceAvailable) {
				ID3D11ShaderResourceView* disoccSRVs[]{
					radianceSRV,
					radianceBSRV,
					_linearDepthTex->srv.get(),
					motionAvailable ? motionSRV : nullptr,
					_prevGeoTex[readIndex]->srv.get(),
					_accumTex[readIndex]->srv.get(),
					_bounceSHTex[readIndex]->srv.get(),
					_bounceCoCgTex[readIndex]->srv.get()
				};
				ID3D11UnorderedAccessView* disoccUAVs[]{
					_radianceTempTex->uav.get(),
					_accumTex[writeIndex]->uav.get(),
					_bounceSHTex[writeIndex]->uav.get(),
					_bounceCoCgTex[writeIndex]->uav.get()
				};
				pass.Dispatch(
					_radianceDisoccCS.get(), disoccSRVs, disoccUAVs, constants, sampler,
					groups8X, groups8Y);
				if (temporalEnabled) {
					++temporalDispatches;
				}

				ID3D11ShaderResourceView* radianceSRVs[]{ _radianceTempTex->srv.get() };
				ID3D11UnorderedAccessView* radianceUAVs[]{
					_radianceMipUAVs[0].get(),
					_radianceMipUAVs[1].get(),
					_radianceMipUAVs[2].get(),
					_radianceMipUAVs[3].get(),
					_radianceMipUAVs[4].get()
				};
				pass.Dispatch(
					_prefilterRadianceCS.get(), radianceSRVs, radianceUAVs, constants, sampler,
					groups16X, groups16Y);
			}

			ID3D11ShaderResourceView* depthPrefilterSRVs[]{ _linearDepthTex->srv.get() };
			ID3D11UnorderedAccessView* depthPrefilterUAVs[]{
				_workingDepthMipUAVs[0].get(),
				_workingDepthMipUAVs[1].get(),
				_workingDepthMipUAVs[2].get(),
				_workingDepthMipUAVs[3].get(),
				_workingDepthMipUAVs[4].get()
			};
			pass.Dispatch(
				_prefilterCS.get(), depthPrefilterSRVs, depthPrefilterUAVs, constants, sampler,
				groups16X, groups16Y);

			if (_prefilterNormalCS) {
				ID3D11ShaderResourceView* normalPrefilterSRVs[]{ _viewNormalMip0SRV.get() };
				ID3D11UnorderedAccessView* normalPrefilterUAVs[]{
					_viewNormalMipUAVs[1].get(),
					_viewNormalMipUAVs[2].get(),
					_viewNormalMipUAVs[3].get(),
					_viewNormalMipUAVs[4].get()
				};
				pass.Dispatch(
					_prefilterNormalCS.get(), normalPrefilterSRVs, normalPrefilterUAVs,
					constants, sampler, groups16X, groups16Y);
			}

			if (radianceAvailable) {
				ID3D11ShaderResourceView* giSRVs[]{
					_workingDepthTex->srv.get(),
					_viewNormalTex->srv.get(),
					_radianceTex->srv.get(),
					_noiseSRV.get(),
					_accumTex[writeIndex]->srv.get(),
					_bounceSHTex[writeIndex]->srv.get(),
					_bounceCoCgTex[writeIndex]->srv.get()
				};
				ID3D11UnorderedAccessView* giUAVs[]{
					_aoRawTex->uav.get(),
					_bounceSHRawTex->uav.get(),
					_bounceCoCgRawTex->uav.get(),
					_prevGeoTex[writeIndex]->uav.get()
				};
				pass.Dispatch(
					_bounceCS.get(), giSRVs, giUAVs, constants, sampler, groups8X, groups8Y);
				if (temporalEnabled) {
					++temporalDispatches;
				}
				_bounceOutputsDirty = true;
				_bounceProducedLastFrame.store(true, std::memory_order_relaxed);
			} else {
				ID3D11ShaderResourceView* aoSRVs[]{
					_workingDepthTex->srv.get(),
					_viewNormalTex->srv.get(),
					nullptr,
					_noiseSRV.get()
				};
				ID3D11UnorderedAccessView* aoUAVs[]{ _aoRawTex->uav.get() };
				pass.Dispatch(
					_aoCS.get(), aoSRVs, aoUAVs, constants, sampler, groups8X, groups8Y);
			}
			_occlusionOutputsDirty = true;
			_aoProducedLastFrame.store(true, std::memory_order_relaxed);

			const bool denoiseBounce =
				radianceAvailable && _settings.denoiseEnabled && _bounceDenoiseCS;
			if (denoiseBounce) {
				ID3D11ShaderResourceView* denoiseSRVs[]{
					_workingDepthTex->srv.get(),
					_viewNormalTex->srv.get(),
					_aoRawTex->srv.get(),
					_bounceSHRawTex->srv.get(),
					_bounceCoCgRawTex->srv.get(),
					_accumTex[writeIndex]->srv.get()
				};
				ID3D11UnorderedAccessView* denoiseUAVs[]{
					_aoDenoisedTex->uav.get(),
					_bounceSHTex[writeIndex]->uav.get(),
					_bounceCoCgTex[writeIndex]->uav.get(),
					_accumBlurTex->uav.get()
				};
				pass.Dispatch(
					_bounceDenoiseCS.get(), denoiseSRVs, denoiseUAVs, constants, sampler,
					groups8X, groups8Y);
				if (temporalEnabled) {
					++temporalDispatches;
				}
				context->CopyResource(
					_accumTex[writeIndex]->resource.get(), _accumBlurTex->resource.get());
				_aoDenoisedLastFrame.store(true, std::memory_order_relaxed);
				_bounceDenoisedLastFrame.store(true, std::memory_order_relaxed);
			} else {
				if (_settings.denoiseEnabled && _denoiseCS) {
					ID3D11ShaderResourceView* denoiseSRVs[]{
						_workingDepthTex->srv.get(),
						_viewNormalTex->srv.get(),
						_aoRawTex->srv.get()
					};
					ID3D11UnorderedAccessView* denoiseUAVs[]{ _aoDenoisedTex->uav.get() };
					pass.Dispatch(
						_denoiseCS.get(), denoiseSRVs, denoiseUAVs, constants, sampler,
						groups8X, groups8Y);
					_aoDenoisedLastFrame.store(true, std::memory_order_relaxed);
				}
				if (radianceAvailable) {
					context->CopyResource(
						_bounceSHTex[writeIndex]->resource.get(), _bounceSHRawTex->resource.get());
					context->CopyResource(
						_bounceCoCgTex[writeIndex]->resource.get(), _bounceCoCgRawTex->resource.get());
				}
			}

			if (radianceAvailable) {
				_history.Publish(frameIndex);
				_prevCamera = camera;
				_prevCameraValid = true;
				_prevFrameW = frameW;
				_prevFrameH = frameH;
			} else {
				ClearBounceOutputs(context);
			}
			_temporalDispatchesLastFrame.store(temporalDispatches, std::memory_order_relaxed);
		} catch (const std::exception& e) {
			ResetHistory(ssgi::HistoryResetReason::kGenerationFailed);
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			_aoProducedLastFrame.store(false, std::memory_order_relaxed);
			_aoDenoisedLastFrame.store(false, std::memory_order_relaxed);
			_bounceProducedLastFrame.store(false, std::memory_order_relaxed);
			_bounceDenoisedLastFrame.store(false, std::memory_order_relaxed);
			_radianceAvailableLastFrame.store(false, std::memory_order_relaxed);
			_historyValidLastFrame.store(false, std::memory_order_relaxed);
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "SSGI generation failed: {}", e.what());
			}
		} catch (...) {
			ResetHistory(ssgi::HistoryResetReason::kGenerationFailed);
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			_aoProducedLastFrame.store(false, std::memory_order_relaxed);
			_aoDenoisedLastFrame.store(false, std::memory_order_relaxed);
			_bounceProducedLastFrame.store(false, std::memory_order_relaxed);
			_bounceDenoisedLastFrame.store(false, std::memory_order_relaxed);
			_radianceAvailableLastFrame.store(false, std::memory_order_relaxed);
			_historyValidLastFrame.store(false, std::memory_order_relaxed);
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "SSGI generation failed.");
			}
		}
	}

	void ScreenSpaceGI::SaveCompositionBindings()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ?
			reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			nullptr;
		if (!_compositionBindingSnapshot.Save(context, kCompositionPSSlot) &&
			_compositionBindingSnapshot.IsSaved()) {
			CS_LOG_ONCE(
				L,
				spdlog::level::err,
				"SSGI composition binding scopes overlap; preserving the active snapshot.");
		}
	}

	void ScreenSpaceGI::RestoreCompositionBindings()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ?
			reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			nullptr;
		_compositionBindingSnapshot.Restore(context);
	}

	void ScreenSpaceGI::BindComposition(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !_started.load(std::memory_order_acquire)) {
			return;
		}

		const bool useDenoisedAO = _aoDenoisedLastFrame.load(std::memory_order_relaxed);
		auto* albedoSRV =
			cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferAlbedo);
		const bool compositionReady =
			_aoProducedLastFrame.load(std::memory_order_relaxed) &&
			IsGeneratorReady() &&
			albedoSRV;
		const auto publishedIndex = _history.ReadIndex();
		ID3D11ShaderResourceView* composition[kCompositionPSSlotCount] = {
			compositionReady ? SRVOf(useDenoisedAO ? _aoDenoisedTex : _aoRawTex) : nullptr,
			compositionReady ? SRVOf(_bounceSHTex[publishedIndex]) : nullptr,
			compositionReady ? SRVOf(_bounceCoCgTex[publishedIndex]) : nullptr,
			compositionReady ? albedoSRV : nullptr
		};
		a_context->PSSetShaderResources(
			kCompositionPSSlot, kCompositionPSSlotCount, composition);

		_albedoBoundLastFrame.store(compositionReady, std::memory_order_relaxed);
		_compositionBindsLastFrame.fetch_add(1, std::memory_order_relaxed);
		if (!albedoSRV) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"SSGI albedo source is unavailable; composition is neutral for this draw.");
		}
	}

	void ScreenSpaceGI::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto resetReason = static_cast<ssgi::HistoryResetReason>(
			_lastResetReason.load(std::memory_order_relaxed));
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("injection_registered", _injectionRegistered.load(std::memory_order_acquire))
			.Field("resources_ready", _resourcesReady.load(std::memory_order_acquire))
			.Field("resource_init_failed", _resourceInitFailed.load(std::memory_order_acquire))
			.Field("ao_produced", _aoProducedLastFrame.load(std::memory_order_relaxed))
			.Field("ao_denoised", _aoDenoisedLastFrame.load(std::memory_order_relaxed))
			.Field("radiance_available", _radianceAvailableLastFrame.load(std::memory_order_relaxed))
			.Field("bounce_produced", _bounceProducedLastFrame.load(std::memory_order_relaxed))
			.Field("bounce_denoised", _bounceDenoisedLastFrame.load(std::memory_order_relaxed))
			.Field("albedo_bound", _albedoBoundLastFrame.load(std::memory_order_relaxed))
			.Field("history_valid", _historyValidLastFrame.load(std::memory_order_relaxed))
			.Field("motion_available", _motionAvailableLastFrame.load(std::memory_order_relaxed))
			.Field(
				"temporal_dispatches",
				static_cast<std::int64_t>(
					_temporalDispatchesLastFrame.load(std::memory_order_relaxed)))
			.Field(
				"reset_count",
				static_cast<std::int64_t>(_historyResetCount.load(std::memory_order_relaxed)))
			.Field("last_reset_reason", std::string_view(ssgi::HistoryResetReasonName(resetReason)))
			.Field("tiled_predicate_available", _tiledPredicateAvailable.load(std::memory_order_relaxed))
			.Field("tiled_lighting_active", _tiledLightingActive.load(std::memory_order_relaxed))
			.Field("tiled_b_available", _tiledBAvailable.load(std::memory_order_relaxed))
			.Field(
				"radiance_source_count",
				static_cast<std::int64_t>(_radianceSourceCount.load(std::memory_order_relaxed)))
			.Field(
				"repeat_callbacks",
				static_cast<std::int64_t>(_repeatCallbacks.load(std::memory_order_relaxed)))
			.Field("contaminated_light_classes", kContaminatedLightClasses)
			.Field("contaminated_routes", kContaminatedRoutes)
			.Field(
				"composition_binds",
				static_cast<std::int64_t>(
					_compositionBindsLastFrame.load(std::memory_order_relaxed)))
			.Dimensions("working", _allocW, _allocH)
			.Field("generation", static_cast<std::int64_t>(_generation));
	}

	void ScreenSpaceGI::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);

		changed |= ImGui::SliderInt("Slices", &_settings.numSlices, 1, 8);
		changed |= ImGui::SliderInt("Steps", &_settings.numSteps, 4, 32);
		changed |= ImGui::SliderFloat("AO radius (game units)", &_settings.aoRadius, 16.0f, 512.0f);
		changed |= ImGui::SliderFloat("GI radius (game units)", &_settings.giRadius, 16.0f, 512.0f);
		changed |= ImGui::SliderFloat("AO power", &_settings.aoPower, 0.5f, 5.0f);
		changed |= ImGui::SliderFloat("Bounce strength", &_settings.bounceStrength, 0.0f, 8.0f);
		changed |= ImGui::Checkbox("Denoise", &_settings.denoiseEnabled);
		changed |= ImGui::SliderFloat("Denoise radius", &_settings.denoiseRadius, 0.5f, 4.0f);
		changed |= ImGui::Checkbox("Temporal denoiser", &_settings.enableTemporalDenoiser);
		float depthDisocclusionPercent = _settings.depthDisocclusion * 100.0f;
		if (ImGui::SliderFloat(
				"Depth disocclusion", &depthDisocclusionPercent, 0.0f, 20.0f, "%.1f%%")) {
			_settings.depthDisocclusion = depthDisocclusionPercent * 0.01f;
			changed = true;
		}
		changed |= ImGui::SliderInt("Max accumulated frames", &_settings.maxAccumFrames, 1, 64);
		changed |= ImGui::SliderFloat(
			"Depth fade start (game units)", &_settings.depthFadeStart, 0.0f, 60000.0f);
		changed |= ImGui::SliderFloat(
			"Depth fade end (game units)", &_settings.depthFadeEnd, 0.0f, 80000.0f);

		if (changed) {
			SaveSettings();
		}

		const char* status = _resourceInitFailed.load(std::memory_order_acquire) ? "failed" :
			(_resourcesReady.load(std::memory_order_acquire) ? "ready" : "not ready");
		ImGui::TextDisabled(
			"Resources: %s (%ux%u) | composition binds: %u | generation: %u",
			status,
			_allocW,
			_allocH,
			_compositionBindsLastFrame.load(std::memory_order_relaxed),
			_generation);
		ImGui::TextDisabled(
			"History: %s | motion: %s | radiance sources: %u | resets: %u (%s)",
			_historyValidLastFrame.load(std::memory_order_relaxed) ? "in use" : "seeding",
			_motionAvailableLastFrame.load(std::memory_order_relaxed) ? "yes" : "no",
			_radianceSourceCount.load(std::memory_order_relaxed),
			_historyResetCount.load(std::memory_order_relaxed),
			ssgi::HistoryResetReasonName(
				static_cast<ssgi::HistoryResetReason>(
					_lastResetReason.load(std::memory_order_relaxed))));
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void ScreenSpaceGI::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(ScreenSpaceGI::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
