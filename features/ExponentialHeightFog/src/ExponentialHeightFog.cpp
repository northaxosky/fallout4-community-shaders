#include "ExponentialHeightFog.h"

#include <DearModdingUI/Client.h>
#include <bit>
#include <cmath>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
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
	FOG_ABI(fogHeight2);
	FOG_ABI(fogHeightFalloff2);
	FOG_ABI(fogDensity2);
	FOG_ABI(directionalInscatteringMultiplier);
	FOG_ABI(directionalInscatteringAnisotropy);
	FOG_ABI(useSkyIBL);
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
	FOG_ABI(volumetricNearGridDistance);
	FOG_ABI(volumetricFarGridPixelSize);
	FOG_ABI(volumetricFarGridSizeZ);
	FOG_ABI(volumetricLocalLightScatteringIntensity);
	FOG_ABI(volumetricFogNoiseScale);
	FOG_ABI(volumetricFogNoiseThreshold);
	FOG_ABI(pad3);
	FOG_ABI(volumetricFogNoiseVelocity);
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
				text = "Applies vanilla fade brightness to exponential height fog.\nNot available in Fallout 4 yet; this setting currently has no effect.";
			else if (a_key == "volumetricSampleJitterMultiplier")
				text = "Adds per-voxel random offset on top of the Halton sequence.";
			else if (a_key == "volumetricUpsampleJitterMultiplier")
				text = "Jitters the final 3D fog lookup in screen space to hide\nlow-resolution froxel pixelization.";
			else if (a_key == "fogDensity2")
				text = "Adds a second stacked exponential height fog layer with its own base height, density and height falloff.\nThe two line integrals are summed.\nUse it for high-altitude haze above the ground layer or a distinct low-lying ground fog.";
			else if (a_key == "volumetricNearGridDistance")
				text = "Distance covered by the full-resolution near volume.\nA second, coarser far volume covers the remaining distance up to the Volumetric View Distance.\nSmaller values improve near-field resolution; larger values move the low-resolution far volume farther away.";
			else if (a_key == "volumetricFogNoiseScale")
				text = "Modulates the volumetric fog density with a 3D value noise field.\nNoise Scale: spatial frequency of the fog clumps (0 = disabled).\nNoise Threshold: soft cutoff that carves clumps out of the noise.\nNoise Velocity: animation drift of the noise field, scaled by time.";
			if (text && dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", text);
		}

		const char* FloatFormat(std::string_view a_key)
		{
			if (a_key == "volumetricShadowBias")
				return "%.4f";
			if (a_key == "fogHeightFalloff" || a_key == "fogHeightFalloff2" || a_key == "fogDensity" || a_key == "fogDensity2" || a_key == "directionalInscatteringAnisotropy")
				return "%.3f";
			if (a_key == "startDistance" || a_key == "fogHeight" || a_key == "fogHeight2" || a_key == "cubemapMipLevel" || a_key == "volumetricDepthDistributionScale")
				return "%.1f";
			if (a_key == "volumetricFogNoiseScale")
				return "%.6f";
			if (a_key == "volumetricFogDistance" || a_key == "volumetricFogStartDistance" || a_key == "volumetricFogNearFadeInDistance" || a_key == "volumetricNearGridDistance")
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
		engine::RegisterPreDeferredLightsImpl([this] { RenderFrame(); Prepass(); }, engine::HookPriority::Late);
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
		L->warn("Fog directional cascade input is unavailable: world-to-shadow transform/split contract needs RE. IBL, CloudShadows and local-light providers are absent.");
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
					_frameSettings, *camera, graphics->frameCount, graphics->GetTAAState() == RE::BSGraphics::TAA_STATE::kEnabled)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Fog dispatch inputs are unavailable; retaining native fog.");
				return;
			}
			_volumetricActive.store(_frameSettings.enabled && _frameSettings.volumetricFogEnabled &&
										(_frameSettings.fogDensity > 0 || _frameSettings.fogDensity2 > 0),
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

	void ExponentialHeightFog::Prepass()
	{
		if (auto* context = engine::GetImmediateContext()) {
			auto* volume = _volume.Integrated();
			engine::BindFrameShaderResources(context, engine::ShaderStage::kPixel, 19, 1, &volume);
			auto* farVolume = _volume.IntegratedFar();
			engine::BindFrameShaderResources(context, engine::ShaderStage::kPixel, 22, 1, &farVolume);
		}
	}

	void ExponentialHeightFog::Bind(ID3D11DeviceContext* a_context, std::uint32_t a_sampler)
	{
		auto* sampler = _volume.Sampler();
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
			.Field("consumer_binds", static_cast<std::int64_t>(_binds.load()))
			.Field("failures", static_cast<std::int64_t>(_failures.load()))
			.Dimensions("volume", _width.load(), _height.load())
			.Field("depth_slices", static_cast<std::int64_t>(_slices.load()));
		_volume.CollectTelemetry(a_sink);
	}

	void ExponentialHeightFog::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		bool changed = false, volume = false, debug = false;
		std::optional<ui::Section> section;
		bool sectionOpen = true;
		const auto begin = [&](const char* a_id, const char* a_title) {
			section.reset();
			section.emplace(a_id, a_title);
			sectionOpen = static_cast<bool>(*section);
		};
		const auto beginDebug = [&](std::size_t a_count) {
			section.reset();
			debug = _settings.volumetricFogEnabled;
			sectionOpen = false;
			if (debug) {
				section.emplace("ehf-volumetric-debug", "Volumetric Debug", ui::Collapsible{ a_count });
				sectionOpen = static_cast<bool>(*section);
			}
		};
		std::size_t debugFields{};
		bool inDebugFields = false;
		std::apply([&](const auto&... fields) {
			((inDebugFields |= fields.key == "volumetricGridPixelSize", debugFields += inDebugFields), ...);
		},
			ehf::kSchema.fields);
		std::apply([&](const auto&... fields) {
			const auto draw = [&](const auto& field) {
				if (field.key == "enabled")
					begin("ehf-fog", "Fog");
				else if (field.key == "fogInscatteringColor")
					begin("ehf-inscattering", "Inscattering");
				else if (field.key == "disableVanillaFog")
					begin("ehf-vanilla", "Vanilla Fog");
				else if (field.key == "useDynamicCubemaps")
					begin("ehf-cubemaps", "Cubemaps");
				else if (field.key == "volumetricFogEnabled") {
					begin("ehf-volumetric", "Volumetric Fog");
					volume = true;
				} else if (field.key == "volumetricGridPixelSize")
					beginDebug(debugFields);
				if (!sectionOpen)
					return;
				if (volume && field.key != "volumetricFogEnabled" && !_settings.volumetricFogEnabled)
					return;
				auto& value = _settings.*field.member;
				const auto label = std::string(field.description);
				using T = typename std::remove_cvref_t<decltype(field)>::ValueType;
				if constexpr (std::same_as<T, settings::Float3> || std::same_as<T, settings::Color4>) {
					// The forwarding ABI exposes scalar inputs but no vector or color editor.
					constexpr const char* components[]{ "R", "G", "B", "A" };
					constexpr const char* axes[]{ "X", "Y", "Z" };
					dmui::ui::Text("%s", label.c_str());
					for (std::size_t i = 0; i < value.size(); ++i) {
						const char* name = std::same_as<T, settings::Float3> ? axes[i] : components[i];
						const std::string component = std::string(name) + "##" + std::string(field.key);
						float candidate = value[i];
						const bool edited = dmui::ui::InputScalar(component.c_str(), &candidate);
						if (edited && !std::isfinite(candidate)) {
							L->warn("Rejected non-finite {} component {}", field.key, name);
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
		section.reset();
		if (changed)
			PublishSettings();
		Menu::Get().DrawDebugViewSelector(*this);
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
