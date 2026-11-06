#include "HDRDisplay.h"

#include <algorithm>

#include <DearModdingUI/Client.h>

#include "HDRDetection.h"
#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/HookRegistry.h"
#include "Render/RenderHooks.h"
#include "Render/SharedDataLayout.h"
#include "Render/TemporalPipeline.h"
#include "Settings/SettingsPersistence.h"
#include "Utils/CSUtil.h"
#include "World/Sky.h"

#include <array>
#include <cmath>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.hdrdisplay");

		// SharedData::HDRData.w: how the frame is framed for the tonemap.
		constexpr float kMenuSceneGameplay = 0.0f;
		constexpr float kMenuScenePaused = 0.58f;
		constexpr float kMenuSceneMainOrLoading = 1.0f;

		// The pause freeze-frame stores copy from RT0, so they follow its format.
		constexpr auto kMenuBackground = RE::BSGraphics::RenderTargetID::kMenuBackground;
		constexpr auto kFreezeFrameStore = static_cast<RE::BSGraphics::RenderTargetID>(54);
		constexpr auto kHdrSceneFormat = static_cast<RE::BSGraphics::Format>(DXGI_FORMAT_R16G16B16A16_FLOAT);

		struct RenderTargetManager_CreateRenderTarget
		{
			static void thunk(RE::BSGraphics::RenderTargetManager* a_manager, RE::BSGraphics::RenderTargetID a_id,
				const RE::BSGraphics::RenderTargetProperties* a_properties, RE::BSGraphics::RenderTargetCreationMode a_mode)
			{
				if (a_properties && (a_id == kMenuBackground || a_id == kFreezeFrameStore) &&
					render::TemporalPipeline::Get().IsHdrContainerActive()) {
					auto promoted = *a_properties;
					promoted.format = kHdrSceneFormat;
					func(a_manager, a_id, &promoted, a_mode);
					return;
				}
				func(a_manager, a_id, a_properties, a_mode);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	HDRDisplay* HDRDisplay::GetSingleton()
	{
		static HDRDisplay instance;
		return &instance;
	}

	bool HDRDisplay::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(hdr_display::kSchema, a_config, candidate, a_error))
			return false;
		// Paper white stays below peak, as upstream clamps on load.
		candidate.paperWhite = std::min(candidate.paperWhite, candidate.peakNits - 1);
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(hdr_display::kSchema, _settings, [this] {
			_settings.paperWhite = std::min(_settings.paperWhite, _settings.peakNits - 1);
		});
		return true;
	}

	bool HDRDisplay::SaveSettings()
	{
		return settings::SaveDelta(hdr_display::kSchema, GetConfigKey(), _settings, *L);
	}

	void HDRDisplay::RestoreDefaultSettings()
	{
		const bool autoDetected = _settings.autoDetected;
		_settings = Settings{};
		// Defaults re-seed from the display, like upstream.
		_settings.enableHDR = _windowsHdrOn.load(std::memory_order_acquire);
		_settings.autoDetected = autoDetected;
		SaveSettings();
	}

	void HDRDisplay::SeedFromDisplay(HWND a_window)
	{
		const auto capability = hdr_display::QueryDisplayCapability(a_window);
		if (!capability)
			return;
		L->info("HDR display detection: supported={}, enabled={}", capability->hdrSupported, capability->hdrEnabled);
		_displaySupportsHdr.store(capability->hdrSupported, std::memory_order_release);
		_windowsHdrOn.store(capability->hdrEnabled, std::memory_order_release);
		_displayKnown.store(true, std::memory_order_release);
		if (_settings.autoDetected)
			return;
		_settings.enableHDR = capability->hdrEnabled;
		_settings.autoDetected = true;
		SaveSettings();
	}

	bool HDRDisplay::IsHdrActive() const noexcept
	{
		return _settings.enableHDR && render::TemporalPipeline::Get().IsHdrContainerActive();
	}

	render::hdr::OutputSettings HDRDisplay::ConsumeOutputSettings() noexcept
	{
		return {
			.enabled = _settings.enableHDR,
			.paperWhite = static_cast<float>(_settings.paperWhite),
			.peakNits = static_cast<float>(_settings.peakNits),
			// Only a frame seeded in OnPostImagespace carries the interface scale.
			.uiBrightness = _seededInterface.exchange(1.0f, std::memory_order_acq_rel)
		};
	}

	void HDRDisplay::Load()
	{
		static bool renderTargetsInstalled = false;
		if (!engine::EnsureDetourInstalled<RenderTargetManager_CreateRenderTarget>(
				renderTargetsInstalled, RE::ID::BSGraphics::RenderTargetManager::CreateRenderTarget, "RenderTargetManager::CreateRenderTarget")) {
			FailLoad("HDR Display could not hook render target creation.");
			return;
		}
		if (!engine::RegisterFeatureShaderBindings(*this)) {
			FailLoad("HDR Display could not register its imagespace shader define.");
			return;
		}
		if (!engine::RegisterPostImagespace(*this, [this] { OnPostImagespace(); })) {
			FailLoad("HDR Display needs the post-imagespace hook.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	void HDRDisplay::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		try {
			_seedPass.Create(a_device);
			D3D11_BLEND_DESC blendDesc{};
			auto& target = blendDesc.RenderTarget[0];
			target.BlendEnable = TRUE;
			// dst * src.rgb; alpha is kept.
			target.SrcBlend = D3D11_BLEND_ZERO;
			target.DestBlend = D3D11_BLEND_SRC_COLOR;
			target.BlendOp = D3D11_BLEND_OP_ADD;
			target.SrcBlendAlpha = D3D11_BLEND_ZERO;
			target.DestBlendAlpha = D3D11_BLEND_ONE;
			target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX::ThrowIfFailed(a_device->CreateBlendState(&blendDesc, _seedBlend.put()));
			D3D11_BUFFER_DESC constants{};
			constants.ByteWidth = 16;
			constants.Usage = D3D11_USAGE_DYNAMIC;
			constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			DX::ThrowIfFailed(a_device->CreateBuffer(&constants, nullptr, _seedConstants.put()));
			render::annotation::SetName(_seedConstants.get(), "HDRDisplay/Seed.Constants");
			_seedPS = util::CompilePixelShader("HDRDisplay/SeedScalePS", L"Data\\Shaders\\FO4\\HDRDisplay\\SeedScalePS.hlsl");
		} catch (const std::exception& e) {
			L->error("The interface seed pass could not be built: {}", e.what());
		}
		if (!_seedPS || !_seedPass.Ready() || !_seedBlend || !_seedConstants)
			FailLoad("HDR Display could not build its interface seed pass");
	}

	void HDRDisplay::WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4)
	{
		a_fo4.HDRLutMethod = static_cast<std::uint32_t>(_settings.lutMethod);
	}

	void HDRDisplay::OnPostImagespace()
	{
		if (!IsHdrActive())
			return;
		auto* context = engine::GetImmediateContext();
		if (!context)
			return;
		RenderReShade(*context);
		SeedInterfaceBrightness(*context);
	}

	void HDRDisplay::RenderReShade(ID3D11DeviceContext& a_context)
	{
		auto& pipeline = render::TemporalPipeline::Get();
		if (!pipeline.ReShadeEffectsEnabled())
			return;
		const auto* graphics = engine::GetGraphicsState();
		const auto camera = graphics ? engine::GetCapturedWorldCameraRecord(graphics->frameCount) : std::nullopt;
		auto* target = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		if (!camera || !target)
			return;
		winrt::com_ptr<ID3D11Resource> resource;
		target->GetResource(resource.put());
		// Before the UI and the interface seed, the scene's white is paper white.
		pipeline.RenderReShadeEffects(a_context, *resource, render::UpdateOutputCanonicalDepth(&a_context, *camera),
			static_cast<float>(_settings.paperWhite));
	}

	void HDRDisplay::SeedInterfaceBrightness(ID3D11DeviceContext& a_context)
	{
		const float interfaceBrightness = _settings.uiBrightness;
		if (interfaceBrightness == 1.0f || !_seedPS)
			return;
		auto* target = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		if (!target)
			return;
		winrt::com_ptr<ID3D11Resource> resource;
		target->GetResource(resource.put());
		const auto texture = resource.try_as<ID3D11Texture2D>();
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (!texture || FAILED(a_context.Map(_seedConstants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return;
		// RT0 is gamma 2.2, so a linear 1 / ui scale is a power of the same factor.
		const float scale = std::pow(1.0f / interfaceBrightness, 1.0f / 2.2f);
		*static_cast<float*>(mapped.pData) = scale;
		a_context.Unmap(_seedConstants.get(), 0);
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		const render::FullscreenPass::Scope pass(_seedPass, &a_context, target, desc.Width, desc.Height);
		if (!pass.IsActive())
			return;
		render::annotation::ScopedEvent annotationScope("HDRDisplay/SeedInterface");
		constexpr float kBlendFactor[4]{};
		a_context.OMSetBlendState(_seedBlend.get(), kBlendFactor, 0xFFFFFFFFu);
		ID3D11Buffer* constants = _seedConstants.get();
		a_context.PSSetConstantBuffers(0, 1, &constants);
		pass.Draw(_seedPS.get());
		_seededInterface.store(interfaceBrightness, std::memory_order_release);
	}

	void HDRDisplay::WriteSharedFrameData(render::SharedDataCB& a_shared)
	{
		const auto* ui = RE::UI::GetSingleton();
		float menuScene = kMenuSceneGameplay;
		if (engine::IsMainOrLoadingMenuOpen())
			menuScene = kMenuSceneMainOrLoading;
		else if (ui && ui->GameIsPaused())
			menuScene = kMenuScenePaused;
		a_shared.HDRData = {
			IsHdrActive() ? 1.0f : 0.0f,
			static_cast<float>(_settings.paperWhite),
			static_cast<float>(_settings.peakNits),
			menuScene
		};
	}

	void HDRDisplay::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(hdr_display::kSchema, _settings);
		constexpr auto clamped = dmui::ui::SliderFlags::kAlwaysClamp;

		if (const ui::Section section{ "hdr-display-output", "Output" }; section) {
			if (!render::TemporalPipeline::Get().IsHdrContainerActive())
				dmui::ui::TextDisabled("HDR output is inactive; it needs borderless windowed mode.");
			else if (_displayKnown.load(std::memory_order_acquire))
				dmui::ui::TextDisabled(_windowsHdrOn.load(std::memory_order_acquire)       ? "Windows HDR is on." :
									   _displaySupportsHdr.load(std::memory_order_acquire) ? "Windows HDR is off for this display." :
																							 "This display does not report HDR.");
			field(&Settings::enableHDR);
			field(&Settings::paperWhite, { .format = "%d nits", .flags = clamped });
			field(&Settings::peakNits, { .format = "%d nits", .flags = clamped });
			field(&Settings::uiBrightness, { .format = "%.2f", .flags = clamped });
			static const std::array lutMethods{
				dmui::ChoiceOption<hdr_display::LutMethod>{ hdr_display::LutMethod::kRadial, "Radial", "radial" },
				dmui::ChoiceOption<hdr_display::LutMethod>{ hdr_display::LutMethod::kGradeTransfer, "Grade transfer", "grade-transfer" }
			};
			field.Choice(&Settings::lutMethod, lutMethods, { .label = "Color grade above white (dev A/B)" });
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(HDRDisplay::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
