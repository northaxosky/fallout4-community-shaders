#include "RainRefraction.h"

#include <DearModdingUI/Client.h>
#include <d3d11.h>
#include <DirectXTex.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <utility>

#include <toml++/toml.hpp>

#include "DynamicCubemaps.h"
#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.rainrefraction");

		namespace rr = rain_refraction;

		constexpr const wchar_t* kRaindropsPath = L"Data\\Shaders\\RainRefraction\\Raindrops.png";

		// The weather technique carries the rain define; the paired features carry the rest.
		constexpr std::array<std::string_view, 3> kRainPassDefines{ "ENVCUBE_RAIN", "RAIN_REFRACTION", "DYNAMIC_CUBEMAPS" };

		void Tooltip(const char* a_text)
		{
			if (dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", a_text);
		}

		// WIC needs COM on the calling thread; an existing apartment of either kind is fine.
		struct ComScope
		{
			ComScope() : initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
			~ComScope()
			{
				if (initialized)
					CoUninitialize();
			}
			ComScope(const ComScope&) = delete;
			ComScope& operator=(const ComScope&) = delete;
			bool initialized;
		};

		// Loads a raindrop normal map (RGB normal, A coverage) as a mipmapped BC7 texture.
		std::string LoadRaindrops(ID3D11Device* a_device, winrt::com_ptr<ID3D11ShaderResourceView>& a_view)
		{
			const auto fail = [](std::string_view a_step, HRESULT a_result) {
				return std::format("{} failed (HRESULT 0x{:08X})", a_step, static_cast<std::uint32_t>(a_result));
			};

			const ComScope com;
			DirectX::ScratchImage image;
			auto hr = DirectX::LoadFromWICFile(kRaindropsPath, DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, image);
			if (FAILED(hr))
				return fail("Reading Data\\Shaders\\RainRefraction\\Raindrops.png", hr);

			DirectX::ScratchImage mipImage;
			hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mipImage);
			if (FAILED(hr))
				return fail("Generating mipmaps", hr);

			DirectX::ScratchImage bc7Image;
			hr = DirectX::Compress(mipImage.GetImages(), mipImage.GetImageCount(), mipImage.GetMetadata(),
				DXGI_FORMAT_BC7_UNORM, DirectX::TEX_COMPRESS_BC7_QUICK, 1.0f, bc7Image);
			if (FAILED(hr))
				return fail("Compressing the texture", hr);

			const auto& metadata = bc7Image.GetMetadata();
			winrt::com_ptr<ID3D11Resource> texture;
			hr = DirectX::CreateTexture(a_device, bc7Image.GetImages(), bc7Image.GetImageCount(), metadata, texture.put());
			if (FAILED(hr))
				return fail("Creating the GPU texture", hr);
			cs::render::annotation::SetName(texture.get(), "RainRefraction/Raindrops");

			const D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{
				.Format = DXGI_FORMAT_BC7_UNORM,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MostDetailedMip = 0, .MipLevels = static_cast<UINT>(metadata.mipLevels) }
			};
			hr = a_device->CreateShaderResourceView(texture.get(), &viewDesc, a_view.put());
			if (FAILED(hr))
				return fail("Creating the shader resource view", hr);
			cs::render::annotation::SetName(a_view.get(), "RainRefraction/Raindrops.SRV");
			L->info("Loaded raindrop texture: {}x{}, BC7, {} mips.", metadata.width, metadata.height, metadata.mipLevels);
			return {};
		}
	}

	RainRefraction* RainRefraction::GetSingleton()
	{
		static RainRefraction instance;
		return &instance;
	}

	bool RainRefraction::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(rr::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(rr::kSchema, _settings, [this] { PublishSettings(); });
		return true;
	}

	void RainRefraction::PublishSettings()
	{
		const std::lock_guard lock(_settingsMutex);
		_published = _settings;
	}

	bool RainRefraction::SaveSettings()
	{
		return settings::SaveDelta(rr::kSchema, GetConfigKey(), _settings, *L);
	}

	void RainRefraction::Load()
	{
		PublishSettings();
		// The refractive pass samples the Dynamic Cubemaps environment.
		if (!DynamicCubemaps::GetSingleton()->IsLoaded()) {
			FailLoad("Rain Refraction requires Dynamic Cubemaps to be loaded.");
			return;
		}
		if (!engine::RegisterFeatureShaderBindings("RainRefraction", *this, [this](engine::ShaderReplacementRegistration& registration) {
				registration.bind = [this](ID3D11DeviceContext* a_context) { BindRain(a_context); };
			})) {
			FailLoad("Rain Refraction shader contribution registration failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	void RainRefraction::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_registrationsReady.load(std::memory_order_acquire) || !a_device)
			return;

		D3D11_BLEND_DESC blendDesc{};
		blendDesc.RenderTarget[0] = {
			.BlendEnable = TRUE,
			.SrcBlend = D3D11_BLEND_SRC_ALPHA,
			.DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
			.BlendOp = D3D11_BLEND_OP_ADD,
			.SrcBlendAlpha = D3D11_BLEND_ONE,
			.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
			.BlendOpAlpha = D3D11_BLEND_OP_ADD,
			.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL
		};
		std::string status;
		if (FAILED(a_device->CreateBlendState(&blendDesc, _alphaBlend.put()))) {
			status = "Failed to create the alpha blend state";
		} else {
			cs::render::annotation::SetName(_alphaBlend.get(), "RainRefraction/AlphaBlend");
			status = LoadRaindrops(a_device, _raindrops);
		}
		if (!status.empty()) {
			_raindrops = nullptr;
			L->error("Refractive rain disabled: {}.", status);
		}
		{
			const std::lock_guard lock(_statusMutex);
			_textureStatus = std::move(status);
		}
		_resourcesReady.store(_raindrops && _alphaBlend, std::memory_order_release);
	}

	bool RainRefraction::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the shader contribution did not register";
			return false;
		}
		if (!render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b6 carries no rain settings";
			return false;
		}
		if (!engine::ValidateShaderInjectionRoutes("RainRefraction", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	cs::RainRefractionFeatureData RainRefraction::GetCommonBufferData()
	{
		Settings settings;
		{
			const std::lock_guard lock(_settingsMutex);
			settings = _published;
		}
		const bool enable = _injectionsOperational.load(std::memory_order_acquire) &&
		                    _resourcesReady.load(std::memory_order_acquire) &&
		                    DynamicCubemaps::GetSingleton()->GetReflectionsSRV() != nullptr;
		_packedEnable.store(enable, std::memory_order_release);
		return {
			.Enable = enable ? 1u : 0u,
			.MotionStretch = settings.MotionStretch,
			.MotionTransparency = settings.MotionTransparency
		};
	}

	void RainRefraction::OnEnabledChanged(bool a_enabled) noexcept
	{
		if (!a_enabled)
			_packedEnable.store(false, std::memory_order_release);
	}

	void RainRefraction::OnRuntimeQuarantined() noexcept
	{
		_packedEnable.store(false, std::memory_order_release);
	}

	bool RainRefraction::IsRefractiveRainPass() const noexcept
	{
		return std::ranges::all_of(kRainPassDefines, [](std::string_view a_define) {
			return engine::ActiveShaderInjectionVariantHasDefine(engine::ShaderInjectionTarget::kEffect, a_define);
		});
	}

	void RainRefraction::BindRain(ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context || !_packedEnable.load(std::memory_order_acquire) || !IsRefractiveRainPass())
			return;
		auto* raindrops = _raindrops.get();
		engine::BindInjectionShaderResources(a_context, kRaindropSlot, 1, &raindrops);
		constexpr float blendFactor[4]{};
		engine::SetInjectionBlendState(a_context, _alphaBlend.get(), blendFactor, 0xFFFFFFFFu);
		_blendOverrides.fetch_add(1, std::memory_order_relaxed);
	}

	std::string RainRefraction::GetTextureStatus() const
	{
		const std::lock_guard lock(_statusMutex);
		return _textureStatus;
	}

	void RainRefraction::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		const auto status = GetTextureStatus();
		const auto count = [](const std::atomic_uint64_t& a_value) { return a_value.load(std::memory_order_relaxed); };
		a_sink.Field("enable_packed", _packedEnable.load(std::memory_order_relaxed))
			.Field("registrations_ready", _registrationsReady.load(std::memory_order_relaxed))
			.Field("resources_ready", _resourcesReady.load(std::memory_order_relaxed))
			.Field("injection_operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("texture_status", status.empty() ? "loaded" : status)
			.Field("blend_overrides", count(_blendOverrides));
	}

	void RainRefraction::DrawSettings()
	{
		if (const auto status = GetTextureStatus(); !status.empty())
			dmui::ui::TextColored(dmui::ui::GetThemeColor(&DMUI_ThemeColors::statusError), "%s", status.c_str());

		settings::SettingsEdit edit{ *this };
		bool changed = false;
		const auto slider = [&](const char* a_label, float rr::Settings::* a_member, const char* a_tooltip) {
			const auto range = rr::kSchema.EditRange(a_member);
			changed |= edit.Continuous(dmui::ui::SliderScalar(a_label, &(_settings.*a_member), &range.min, &range.max, "%.2f"));
			Tooltip(a_tooltip);
		};
		slider("Motion Stretch", &rr::Settings::MotionStretch,
			"How far drops stretch along their fall velocity.\nLow values give round drops, 1 = vanilla streak length, higher values stretch further.");
		slider("Motion Transparency", &rr::Settings::MotionTransparency,
			"Fades the drops; higher values make rain more transparent.");
		if (changed)
			PublishSettings();
	}

	void RainRefraction::RestoreDefaultSettings()
	{
		_settings = {};
		PublishSettings();
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(RainRefraction::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
