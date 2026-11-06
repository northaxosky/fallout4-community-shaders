#include "HDRDisplay.h"

#include <algorithm>

#include <DearModdingUI/Client.h>

#include "HDRDetection.h"
#include "Host/HostClient.h"
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
			.uiBrightness = _settings.uiBrightness,
			.interfaceSeparated = _interfaceSeparated.exchange(false, std::memory_order_acq_rel)
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
		if (!engine::RegisterPreImagespace(*this, [this] { OnPreImagespace(); }) ||
			!engine::RegisterPreInterface(*this, [this] { SeparateInterface(); }, engine::HookPriority::OutermostPost) || !engine::RegisterPostImagespace(*this, [this] { SeparateInterface(); })) {
			FailLoad("HDR Display needs the imagespace hooks.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	void HDRDisplay::OnPreImagespace()
	{
		auto* context = engine::GetImmediateContext();
		// A swap that survived its frame would leave the engine drawing into the scene texture.
		if (_sceneSwap.IsActive()) {
			L->error("The scene redirect outlived its frame; restoring it.");
			if (context)
				RestoreFrameTarget(*context);
			else
				_sceneSwap.Restore();
		}
		// The host shell blurs the back buffer, so it must hold the whole frame.
		if (!IsHdrActive() || !context || host::HostClient::IsShellOpen())
			return;
		const auto scene = render::TemporalPipeline::Get().GetHdrSceneTarget();
		auto* frameTarget = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		if (!scene || !frameTarget)
			return;
		_sceneSwap.Apply(RE::BSGraphics::RenderTargetID::kFrameBuffer, scene.texture, scene.srv, scene.rtv);
		if (_sceneSwap.IsActive())
			engine::ReplaceBoundRenderTargetView(*context, frameTarget, scene.rtv);
	}

	void HDRDisplay::RestoreFrameTarget(ID3D11DeviceContext& a_context)
	{
		auto* sceneTarget = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		_sceneSwap.Restore();
		engine::ReplaceBoundRenderTargetView(
			a_context, sceneTarget, engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer));
	}

	void HDRDisplay::SeparateInterface()
	{
		if (!_sceneSwap.IsActive())
			return;
		auto* context = engine::GetImmediateContext();
		if (!context) {
			_sceneSwap.Restore();
			return;
		}
		RenderReShade(*context);
		RestoreFrameTarget(*context);
		auto* proxyTarget = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		if (!proxyTarget)
			return;
		// The interface composites over the scene with alpha, so it starts transparent.
		constexpr float kClear[4]{};
		context->ClearRenderTargetView(proxyTarget, kClear);
		_interfaceSeparated.store(true, std::memory_order_release);
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
		// Before the interface, the scene's white is paper white.
		pipeline.RenderReShadeEffects(a_context, *resource, render::UpdateOutputCanonicalDepth(&a_context, *camera),
			static_cast<float>(_settings.paperWhite));
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
