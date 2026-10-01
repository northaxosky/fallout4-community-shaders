#include "ExponentialHeightFog.h"

#include <DearModdingUI/Client.h>
#include <bit>
#include <cmath>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Menu/SettingsEdit.h"
#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "World/Weather.h"

namespace cs::features
{
	namespace ehf = exponential_height_fog;
#define FOG_ABI(member) static_assert(offsetof(ehf::Settings, member) == offsetof(render::ExponentialHeightFogSettings, member))
	FOG_ABI(enabled);
	FOG_ABI(useDynamicCubemaps);
	FOG_ABI(startDistance);
	FOG_ABI(fogHeight);
	FOG_ABI(fogHeightFalloff);
	FOG_ABI(fogDensity);
	FOG_ABI(directionalInscatteringMultiplier);
	FOG_ABI(directionalInscatteringAnisotropy);
	FOG_ABI(inscatteringTint);
	FOG_ABI(cubemapMipLevel);
	FOG_ABI(sunlightAttenuationAmount);
	FOG_ABI(respectVanillaFogFade);
	FOG_ABI(disableVanillaFog);
	FOG_ABI(fogInscatteringColor);
	FOG_ABI(originalFogColorAmount);
	FOG_ABI(volumetricFogEnabled);
	FOG_ABI(volumetricGridPixelSize);
	FOG_ABI(volumetricGridSizeZ);
	FOG_ABI(volumetricFogDistance);
	FOG_ABI(volumetricFogStartDistance);
	FOG_ABI(volumetricFogNearFadeInDistance);
	FOG_ABI(volumetricFogExtinctionScale);
	FOG_ABI(volumetricFogAlbedo);
	FOG_ABI(volumetricFogEmissive);
	FOG_ABI(volumetricDirectionalScatteringIntensity);
	FOG_ABI(volumetricShadowBias);
	FOG_ABI(volumetricDepthDistributionScale);
	FOG_ABI(volumetricSkyLightingIntensity);
	FOG_ABI(volumetricFogScatteringDistribution);
	FOG_ABI(volumetricHistoryWeight);
	FOG_ABI(volumetricHistoryMissSampleCount);
	FOG_ABI(volumetricSampleJitterMultiplier);
	FOG_ABI(volumetricUpsampleJitterMultiplier);
	FOG_ABI(volumetricLocalLightScatteringIntensity);
	FOG_ABI(pad0);
#undef FOG_ABI
	namespace
	{
		auto* L = cs::log::Get("cs.feature.exponentialheightfog");
		constexpr FeatureDebugView kDebugViews[]{
			{ "fog_factor", "Fog Factor" }
		};

		void Tooltip(std::string_view a_key)
		{
			const char* text = nullptr;
			if (a_key == "directionalInscatteringAnisotropy")
				text = "Controls the asymmetry of inscattering via the Henyey-Greenstein phase function.\nPositive values produce forward scattering (glow around sun).\nZero is isotropic. Negative values produce back scattering.";
			else if (a_key == "disableVanillaFog")
				text = "Disables the vanilla fog entirely. Only exponential height fog will be applied.";
			else if (a_key == "respectVanillaFogFade")
				text = "Applies vanilla fade brightness to exponential height fog.\nFO4 fade-brightness mapping is not available.";
			else if (a_key == "volumetricSampleJitterMultiplier")
				text = "Matches UE's r.VolumetricFog.LightScatteringSampleJitterMultiplier.\nAdds per-voxel random offset on top of the Halton sequence.\n0 = UE default; nonzero values need stronger temporal filtering.";
			else if (a_key == "volumetricUpsampleJitterMultiplier")
				text = "Matches UE's r.VolumetricFog.UpsampleJitterMultiplier.\nJitters the final 3D fog lookup in screen space to hide\nlow-resolution froxel pixelization. 0 = UE default.";
			if (text && dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", text);
		}

		const char* FloatFormat(std::string_view a_key)
		{
			if (a_key == "volumetricShadowBias")
				return "%.4f";
			if (a_key == "fogHeightFalloff" || a_key == "fogDensity" || a_key == "directionalInscatteringAnisotropy")
				return "%.3f";
			if (a_key == "startDistance" || a_key == "fogHeight" || a_key == "cubemapMipLevel" || a_key == "volumetricDepthDistributionScale")
				return "%.1f";
			if (a_key == "volumetricFogDistance" || a_key == "volumetricFogStartDistance" || a_key == "volumetricFogNearFadeInDistance")
				return "%.0f";
			return "%.2f";
		}
	}

	ExponentialHeightFog* ExponentialHeightFog::GetSingleton()
	{
		static ExponentialHeightFog instance;
		return &instance;
	}

	std::span<const FeatureDebugView> ExponentialHeightFog::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}

	void ExponentialHeightFog::SetDebugView(std::string_view a_view) noexcept
	{
		_debugFogFactor.store(a_view == "fog_factor", std::memory_order_release);
	}

	FullscreenDebugData ExponentialHeightFog::GetFullscreenDebugData() const noexcept
	{
		return { .owner = FullscreenDebugOwner::ExponentialHeightFog,
			.mode = _debugFogFactor.load(std::memory_order_acquire) ? 1u : 0u };
	}

	bool ExponentialHeightFog::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(ehf::kSchema, a_config, candidate, a_error) ||
			!_weather.Configure(ehf::kSchema, ehf::kWeatherVariables, a_config.get("weather"), a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(ehf::kSchema, _settings, [this] { PublishSettings(); });
		return true;
	}

	void ExponentialHeightFog::PublishSettings()
	{
		const std::lock_guard lock(_settingsMutex);
		_published = _settings;
	}

	bool ExponentialHeightFog::SaveSettings()
	{
		return settings::SaveDelta(ehf::kSchema, GetConfigKey(), _settings, *L);
	}

	void ExponentialHeightFog::Load()
	{
		PublishSettings();
		if (!engine::RegisterFeatureShaderBindings("ExponentialHeightFog", *this, [this](engine::ShaderReplacementRegistration& registration) {
				const auto target = registration.targetId;
				if (target == engine::ShaderInjectionTarget::kBsLighting || target == engine::ShaderInjectionTarget::kBsdfLight)
					return;
				const std::uint32_t sampler = target == engine::ShaderInjectionTarget::kBsdfComposite ? 13u : 15u;
				registration.slotClaims = {
					{ engine::ShaderStage::kPixel, engine::ShaderResourceType::kShaderResource, 19 },
					{ engine::ShaderStage::kPixel, engine::ShaderResourceType::kSampler, sampler,
						engine::ShaderSamplerContract::kLinearClamp }
				};
				registration.bind = [this, sampler](ID3D11DeviceContext* a_context) { Bind(a_context, sampler); };
			})) {
			FailLoad("Exponential height fog shader registration failed.");
			return;
		}
		// FO4: prepare before b6 publication, dispatch after canonical depth and terrain updates.
		if (!engine::RegisterPostDeferredPrePass([this] { PrepareFrame(); }, static_cast<engine::HookPriority>(-150))) {
			FailLoad("Exponential height fog shader or prepass registration failed.");
			return;
		}
		engine::RegisterPreDeferredLightsImpl([this] { RenderFrame(); }, engine::HookPriority::Late);
		if (!engine::RegisterPostForwardSky([this] {
				if (!CanBind() || !_enabled.load(std::memory_order_acquire))
					return;
				auto* renderer = RE::BSGraphics::GetRendererData();
				if (!renderer)
					return;
				try {
					_volume.CompositeSky(reinterpret_cast<ID3D11DeviceContext*>(renderer->context));
				} catch (const std::exception& e) {
					_failures.fetch_add(1, std::memory_order_relaxed);
					CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Sky fog composition failed: {}", e.what());
				}
			},
				engine::HookPriority::Late))
			FailLoad("Exponential height fog sky-composition registration failed.");
	}

	void ExponentialHeightFog::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		_resourcesReady.store(_volume.Initialize(a_device), std::memory_order_release);
		if (!_resourcesReady.load())
			L->error("Exponential height fog compute compilation failed.");
	}

	bool ExponentialHeightFog::ValidateShaderInjections(std::string& a_error)
	{
		if (!_resourcesReady.load() || !render::IsSharedDataReady()) {
			a_error = "Exponential height fog resources or shared substrate are unavailable.";
			return false;
		}
		if (!engine::ValidateShaderInjectionRoutes("ExponentialHeightFog", a_error))
			return false;
		_operational.store(true, std::memory_order_release);
		L->warn("Fog directional cascade input is unavailable: world-to-shadow transform/split contract needs RE. IBL, Skylighting, CloudShadows and local-light providers are absent.");
		return true;
	}

	void ExponentialHeightFog::PrepareFrame()
	{
		_frameReady.store(false, std::memory_order_release);
		_volumetricActive.store(false, std::memory_order_relaxed);
		if (!_operational.load(std::memory_order_acquire))
			return;
		render::annotation::ScopedEvent event("ExponentialHeightFog/prepare");
		Settings settings;
		{
			const std::lock_guard lock(_settingsMutex);
			settings = _published;
		}
		const auto weather = engine::SnapshotWeather();
		const auto key = [](const RE::TESWeather* value) {
			if (value) {
				if (auto* file = value->GetFile(0))
					return decltype(_weather)::Key(value->GetFormID() & (file->IsLight() ? 0xFFFu : 0xFFFFFFu), file->GetFilename());
			}
			return std::string{};
		};
		const auto current = key(weather.current);
		if (weather.previous)
			_previousWeather = key(weather.previous);
		const auto& previous = _previousWeather;
		_frameSettings = _weather.Evaluate(ehf::kSchema, settings, previous, current, weather.transitionPct);
		_enabled.store(_frameSettings.enabled != 0, std::memory_order_relaxed);
		auto* graphics = engine::GetGraphicsState();
		auto* manager = engine::GetRenderTargetManager();
		if (!graphics || !engine::GetWorldCameraRecord()) {
			_volume.Reset();
			_frameSettings.enabled = 0;
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Fog is waiting for the validated world camera.");
			return;
		}
		const float widthRatio = manager ? manager->GetDynamicWidthRatio() : 1.0f;
		const float heightRatio = manager ? manager->GetDynamicHeightRatio() : 1.0f;
		try {
			_volume.Prepare(_frameSettings,
				static_cast<std::uint32_t>(std::ceil(static_cast<float>(graphics->screenWidth) * widthRatio)),
				static_cast<std::uint32_t>(std::ceil(static_cast<float>(graphics->screenHeight) * heightRatio)));
		} catch (const std::exception& e) {
			_volume.Reset();
			_frameSettings.volumetricFogEnabled = 0;
			_failures.fetch_add(1, std::memory_order_relaxed);
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Fog allocation failed; retaining {} fog: {}",
				_volume.SkyReady() ? "analytic" : "native", e.what());
		}
		if (!_volume.SkyReady())
			_frameSettings.enabled = 0;
		_enabled.store(_frameSettings.enabled != 0, std::memory_order_relaxed);
		if (!_volume.Integrated())
			_frameSettings.volumetricFogEnabled = 0;
		const auto grid = _volume.Grid();
		_width.store(grid.x);
		_height.store(grid.y);
		_slices.store(grid.z);
	}

	void ExponentialHeightFog::RenderFrame()
	{
		if (!_operational.load(std::memory_order_acquire))
			return;
		const auto camera = engine::GetWorldCameraRecord();
		auto* graphics = engine::GetGraphicsState();
		auto* renderer = RE::BSGraphics::GetRendererData();
		if (!camera || !graphics || !renderer || !render::GetCanonicalSceneDepthSRV()) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Fog is waiting for current-frame camera, renderer, and canonical depth.");
			return;
		}
		try {
			if (!_volume.Dispatch(reinterpret_cast<ID3D11DeviceContext*>(renderer->context),
					_frameSettings, *camera, graphics->frameCount, *engine::GetTemporalAAEnableGlobal() != 0)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Fog dispatch inputs are unavailable; retaining native fog.");
				return;
			}
			if (_frameSettings.enabled && _frameSettings.volumetricFogEnabled && _frameSettings.fogDensity > 0)
				_dispatches.fetch_add(4, std::memory_order_relaxed);
			_volumetricActive.store(_frameSettings.enabled && _frameSettings.volumetricFogEnabled &&
										_frameSettings.fogDensity > 0,
				std::memory_order_relaxed);
			_frameReady.store(true, std::memory_order_release);
		} catch (const std::exception& e) {
			_volume.Reset();
			_failures.fetch_add(1, std::memory_order_relaxed);
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Fog dispatch failed; retaining native fog: {}", e.what());
		}
	}

	render::ExponentialHeightFogSettings ExponentialHeightFog::GetCommonBufferData() const
	{
		static_assert(sizeof(Settings) == sizeof(render::ExponentialHeightFogSettings));
		if (!_operational.load(std::memory_order_acquire))
			return {};
		return std::bit_cast<render::ExponentialHeightFogSettings>(_frameSettings);
	}

	bool ExponentialHeightFog::CanBind() const
	{
		return _frameReady.load(std::memory_order_acquire) && render::IsSharedDataReady() &&
		       render::GetCanonicalSceneDepthSRV() != nullptr;
	}

	void ExponentialHeightFog::Bind(ID3D11DeviceContext* a_context, std::uint32_t a_sampler)
	{
		auto* volume = _volume.Integrated();
		auto* sampler = _volume.Sampler();
		engine::BindInjectionShaderResources(a_context, 19, 1, &volume);
		engine::BindInjectionSamplers(a_context, a_sampler, 1, &sampler);
		_binds.fetch_add(1, std::memory_order_relaxed);
	}

	void ExponentialHeightFog::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		a_sink.Field("enabled", _enabled.load())
			.Field("injection_operational", _operational.load())
			.Field("published_active", _frameReady.load())
			.Field("volumetric_active", _volumetricActive.load())
			.Field("directional_shadows_available", false)
			.Field("debug_fog_factor", _debugFogFactor.load())
			.Field("dispatches", static_cast<std::int64_t>(_dispatches.load()))
			.Field("consumer_binds", static_cast<std::int64_t>(_binds.load()))
			.Field("failures", static_cast<std::int64_t>(_failures.load()))
			.Dimensions("volume", _width.load(), _height.load())
			.Field("depth_slices", static_cast<std::int64_t>(_slices.load()));
		_volume.CollectTelemetry(a_sink);
	}

	void ExponentialHeightFog::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		bool changed = false, volume = false, debug = false, showDebug = false;
		std::apply([&](const auto&... fields) {
			const auto draw = [&](const auto& field) {
				if (field.key == "volumetricFogEnabled") {
					dmui::ui::Separator();
					dmui::ui::Text("Volumetric Fog");
					volume = true;
				}
				if (field.key == "volumetricGridPixelSize") {
					debug = true;
					if (_settings.volumetricFogEnabled)
						showDebug = dmui::ui::CollapsingHeader("Debug");
				}
				if ((volume && field.key != "volumetricFogEnabled" && !_settings.volumetricFogEnabled) || (debug && !showDebug))
					return;
				auto& value = _settings.*field.member;
				const auto label = std::string(field.description);
				using T = typename std::remove_cvref_t<decltype(field)>::ValueType;
				if constexpr (std::same_as<T, settings::Color4>) {
					// The forwarding ABI exposes scalar inputs but no color editor.
					constexpr const char* components[]{ "R", "G", "B", "A" };
					dmui::ui::Text("%s", label.c_str());
					for (std::size_t i = 0; i < value.size(); ++i) {
						const std::string component = std::string(components[i]) + "##" + std::string(field.key);
						float candidate = value[i];
						const bool edited = dmui::ui::InputScalar(component.c_str(), &candidate);
						if (edited && !std::isfinite(candidate)) {
							L->warn("Rejected non-finite {} component {}", field.key, components[i]);
						} else {
							if (edited)
								value[i] = candidate;
							changed |= edit.Continuous(edited);
						}
					}
				} else {
					if constexpr (std::same_as<T, std::uint32_t>) {
						if (field.edit.max == 1) {
							bool enabled = value != 0;
							const bool toggled = dmui::ui::Checkbox(label.c_str(), &enabled);
							if (toggled)
								value = enabled ? 1u : 0u;
							changed |= edit.Discrete(toggled);
							Tooltip(field.key);
							return;
						}
					}
					const char* format = "%u";
					if constexpr (std::floating_point<T>)
						format = FloatFormat(field.key);
					changed |= edit.Continuous(dmui::ui::SliderScalar(label.c_str(), &value, &field.edit.min, &field.edit.max,
						format, debug ? dmui::ui::SliderFlags::kAlwaysClamp : dmui::ui::SliderFlags::kNone));
					Tooltip(field.key);
				}
			};
			(draw(fields), ...);
		},
			ehf::kSchema.fields);
		if (changed)
			PublishSettings();
		Menu::Get().DrawDebugViewSelector(*this);
		dmui::ui::TextDisabled("Directional cascade scattering, IBL, Skylighting, CloudShadows and local lights are unavailable.");
	}

	void ExponentialHeightFog::RestoreDefaultSettings()
	{
		_settings = {};
		PublishSettings();
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(ExponentialHeightFog::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
