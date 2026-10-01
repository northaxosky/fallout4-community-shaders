#include "WetnessEffects.h"

#include <DearModdingUI/Client.h>
#include <d3d11.h>

#include <array>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Menu/SettingsEdit.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "World/Water.h"
#include "World/Weather.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.wetnesseffects");

		constexpr std::array<FeatureDebugView, 2> kDebugViews{ { { "wetness_term",
																	 "Wetness term",
																	 FeatureDebugViewKind::kFullscreen },
			{ "world_up",
				"World-up response",
				FeatureDebugViewKind::kFullscreen } } };

		ID3D11DeviceContext* GetImmediateContext() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			return rendererData ?
			           reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			           nullptr;
		}
	}

	WetnessEffects* WetnessEffects::GetSingleton()
	{
		static WetnessEffects instance;
		return &instance;
	}

	std::span<const FeatureDebugView> WetnessEffects::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}

	void WetnessEffects::SetDebugView(std::string_view a_view) noexcept
	{
		DebugVisualization visualization = DebugVisualization::kOff;
		if (a_view == "wetness_term")
			visualization = DebugVisualization::kWetnessTerm;
		else if (a_view == "world_up")
			visualization = DebugVisualization::kWorldUp;
		_debugVisualization.store(visualization, std::memory_order_release);
	}

	FullscreenDebugData WetnessEffects::GetFullscreenDebugData() const noexcept
	{
		return { .owner = FullscreenDebugOwner::WetnessEffects,
			.mode = static_cast<std::uint32_t>(_debugVisualization.load(std::memory_order_acquire)) };
	}

	bool WetnessEffects::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(wetness_math::kSchema, a_config, candidate, a_error)) {
			return false;
		}
		_settings = wetness_math::Clamp(candidate);
		_liveSettings = settings::BindLiveSettings(wetness_math::kSchema, _settings);
		return true;
	}

	bool WetnessEffects::SaveSettings()
	{
		return settings::SaveDelta(wetness_math::kSchema, GetConfigKey(), _settings, *L);
	}

	void WetnessEffects::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("WetnessEffects", *this, [this](cs::engine::ShaderReplacementRegistration& registration) {
				const auto a_target = registration.targetId;
				const bool producer = a_target == cs::engine::ShaderInjectionTarget::kDeferredPrepass;
				if (producer) {
					registration.bind = [this](ID3D11DeviceContext* context) { BindFilmOutput(context); };
				}
			})) {
			FailLoad("Wetness shader contribution registration failed.");
			return;
		}
		if (!cs::engine::RegisterPreDeferredPrePass([this] { BeginPrepass(); }) ||
			!cs::engine::RegisterPostDeferredPrePass([this] {
				_inPrepass = false;
				Prepass();
			})) {
			FailLoad("Wetness could not register its deferred material producer");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
		cs::engine::InstallWaterRippleVisibilityFilter([this] {
			return _suppressRipples.load(std::memory_order_relaxed);
		});
		L->info(
			"Registered wetness shader contributions (enabled={}, max_rain_wetness={:.2f}, min_rain_wetness={:.2f}).",
			_settings.enabled,
			_settings.maxRainWetness,
			_settings.minRainWetness);
	}

	bool WetnessEffects::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "shader contributions did not all register";
			_validationDetail = a_error;
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b5 and b6 carry no wetness";
			_validationDetail = a_error;
			return false;
		}

		if (!cs::engine::ValidateShaderInjectionRoutes(
				"WetnessEffects", a_error)) {
			_validationDetail = a_error;
			return false;
		}

		_validationDetail.clear();
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	void WetnessEffects::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		// A non-aliasing t71 presence marker preserves native wetness on rejected producer draws.
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		const std::uint8_t value = 0;
		const D3D11_SUBRESOURCE_DATA initial{ &value, 1, 0 };
		winrt::com_ptr<ID3D11Texture2D> texture;
		DX::ThrowIfFailed(a_device->CreateTexture2D(&desc, &initial, texture.put()));
		DX::ThrowIfFailed(a_device->CreateShaderResourceView(texture.get(), nullptr, _filmAvailabilitySRV.put()));
		cs::render::annotation::SetName(_filmAvailabilitySRV.get(), "WetnessEffects::Film availability");
	}

	void WetnessEffects::BeginPrepass()
	{
		_inPrepass = true;
		_filmReady.store(false, std::memory_order_relaxed);
		auto* context = GetImmediateContext();
		auto* device = cs::engine::GetDevice();
		auto* source = cs::engine::GetRenderTargetTexture(cs::engine::RenderTarget::kGbufferNormal);
		if (!context || !device || !source)
			return;
		ID3D11ShaderResourceView* empty = nullptr;
		context->PSSetShaderResources(71, 1, &empty);
		D3D11_TEXTURE2D_DESC desc{}, current{};
		source->GetDesc(&desc);
		if (_filmTexture)
			_filmTexture->GetDesc(&current);
		if (!_filmTexture || !_filmRTV || !_filmSRV || desc.Width != current.Width || desc.Height != current.Height ||
			desc.SampleDesc.Count != current.SampleDesc.Count) {
			_filmRTV = nullptr;
			_filmSRV = nullptr;
			_filmTexture = nullptr;
			// FO4: the six native MRTs have no universal spare film-normal or roughness channel.
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.MiscFlags = 0;
			if (FAILED(device->CreateTexture2D(&desc, nullptr, _filmTexture.put())) ||
				FAILED(device->CreateRenderTargetView(_filmTexture.get(), nullptr, _filmRTV.put())) ||
				FAILED(device->CreateShaderResourceView(_filmTexture.get(), nullptr, _filmSRV.put())))
				return;
			cs::render::annotation::SetName(_filmTexture.get(), "WetnessEffects::Film");
			cs::render::annotation::SetName(_filmRTV.get(), "WetnessEffects::Film RTV");
			cs::render::annotation::SetName(_filmSRV.get(), "WetnessEffects::Film SRV");
		}
		const float dry[]{ 0.5f, 0.5f, 1.0f, 0.0f };
		context->ClearRenderTargetView(_filmRTV.get(), dry);
		_filmReady.store(true, std::memory_order_relaxed);
	}

	void WetnessEffects::BindFilmOutput(ID3D11DeviceContext* a_context)
	{
		ID3D11RenderTargetView* targets[8]{};
		winrt::com_ptr<ID3D11DepthStencilView> depth;
		const bool ready = _inPrepass && _filmReady.load(std::memory_order_relaxed) && cs::render::IsSharedDataCurrent();
		if (ready)
			a_context->OMGetRenderTargets(8, targets, depth.put());
		const bool valid = ready &&
		                   targets[0] == cs::engine::GetRenderTargetRTV(cs::engine::RenderTarget::kGbufferAlbedo) &&
		                   targets[1] == cs::engine::GetRenderTargetRTV(cs::engine::RenderTarget::kGbufferNormal) &&
		                   !targets[6] && !targets[7];
		bool bound = false;
		if (valid) {
			winrt::com_ptr<ID3D11BlendState> native;
			float factors[4]{};
			UINT mask{};
			a_context->OMGetBlendState(native.put(), factors, &mask);
			auto blends = std::span(_filmBlends).first(_filmBlendCount);
			const auto found = std::ranges::find_if(blends, [&](const auto& entry) { return entry.native == native; });
			FilmBlend* film = found == blends.end() ? nullptr : &*found;
			if (!film) {
				D3D11_BLEND_DESC desc{};
				if (native)
					native->GetDesc(&desc);
				else {
					for (auto& rt : desc.RenderTarget) {
						rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
						rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_ZERO;
						rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
						rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
					}
				}
				if (!desc.IndependentBlendEnable)
					for (auto& rt : desc.RenderTarget) rt = desc.RenderTarget[0];
				desc.IndependentBlendEnable = TRUE;
				desc.RenderTarget[6] = desc.RenderTarget[1];
				desc.RenderTarget[6].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
				FilmBlend entry{ native, {} };
				if (_filmBlendCount < _filmBlends.size() &&
					SUCCEEDED(cs::engine::GetDevice()->CreateBlendState(&desc, entry.film.put()))) {
					cs::render::annotation::SetName(entry.film.get(), "WetnessEffects::Film blend");
					film = &_filmBlends[_filmBlendCount++];
					*film = std::move(entry);
				} else {
					CS_LOG_EVERY_MS(Log(), 2000, spdlog::level::err, "Wetness film blend creation failed or device blend-state limit reached.");
				}
			}
			if (film) {
				cs::engine::CaptureShaderInjectionOutputs(a_context);
				ID3D11ShaderResourceView* nullView = nullptr;
				cs::engine::BindInjectionShaderResources(a_context, 71, 1, &nullView);
				targets[6] = _filmRTV.get();
				a_context->OMSetRenderTargets(7, targets, depth.get());
				a_context->OMSetBlendState(film->film.get(), factors, mask);
				cs::engine::RecordShaderInjectionD3DBinds(2);
				targets[6] = nullptr;
				bound = true;
			}
		}
		for (auto* target : targets)
			if (target)
				target->Release();
		auto* availability = bound ? _filmAvailabilitySRV.get() : nullptr;
		cs::engine::BindInjectionShaderResources(a_context, 71, 1, &availability);
		(bound ? _producerDraws : _producerRejected).fetch_add(1, std::memory_order_relaxed);
	}

	void WetnessEffects::Prepass()
	{
		auto* context = GetImmediateContext();
		if (!context)
			return;
		if (_inPrepass) {
			auto* precip = cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kPrecipitationOcclusion);
			cs::engine::BindInjectionShaderResources(context, 70, 1, &precip);
		} else {
			BindFilmInput(context, false);
			BindFilmInput(context, true);
			BindCompositeResources(context);
		}
	}

	void WetnessEffects::BindFilmInput(ID3D11DeviceContext* a_context, bool a_compute)
	{
		auto* view = _filmReady.load(std::memory_order_relaxed) ? _filmSRV.get() : nullptr;
		if (a_compute) {
			a_context->CSSetShaderResources(71, 1, &view);
		} else {
			cs::engine::BindInjectionShaderResources(a_context, 71, 1, &view);
			auto* precip = cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kPrecipitationOcclusion);
			cs::engine::BindInjectionShaderResources(a_context, 70, 1, &precip);
		}
	}

	cs::WetnessEffectsFeatureData WetnessEffects::GetCommonBufferData() const
	{
		_suppressRipples.store(_injectionsOperational.load(std::memory_order_acquire) &&
								   _settings.enabled && !_settings.enableVanillaRipples,
			std::memory_order_relaxed);
		if (!_injectionsOperational.load(std::memory_order_acquire)) {
			return {};
		}

		const auto weather = cs::engine::SnapshotWeather();
		const bool isExterior = weather.fullSky;
		const auto weatherWetness = wetness_math::ComputeWeatherWetness(
			isExterior,
			weather.previousIsRain,
			weather.previousEndPrecip,
			weather.currentIsRain,
			weather.currentBeginPrecip,
			weather.transitionPct);
		float wetness = wetness_math::PublishedWetness(
			_settings.enabled, weatherWetness.wetness);
		float puddleWetness = wetness_math::PublishedWetness(
			_settings.enabled, weatherWetness.puddleWetness);
		float raining = _settings.enabled && isExterior ? wetness_math::ComputeRaining(
															  weather.currentRainDensity, weather.currentBeginPrecip,
															  weather.previousRainDensity, weather.previousEndPrecip, weather.transitionPct) :
		                                                  0.0f;
		if (_settings.enabled && weather.available) {
			const bool interior = !isExterior && _settings.enableIntExOverride;
			if (_settings.enableWetnessOverride)
				wetness = interior ? _settings.wetnessOverrideInterior : _settings.wetnessOverrideExterior;
			if (_settings.enablePuddleOverride)
				puddleWetness = interior ? _settings.puddleOverrideInterior : _settings.puddleOverrideExterior;
			if (_settings.enableRainOverride)
				raining = interior ? _settings.rainOverrideInterior : _settings.rainOverrideExterior;
		}
		if (const auto* state = cs::engine::GetGraphicsState(); state && state->frameCount != _timerFrame) {
			const auto* main = RE::Main::GetSingleton();
			const auto* timer = RE::BSTimer::GetSingleton();
			if (main && !main->inMenuMode && !main->freezeTime && timer)
				_rainTimer += static_cast<std::uint64_t>(timer->realTimeDelta * 1000.0f);
			_timerFrame = state->frameCount;
		}

		_isExterior.store(isExterior, std::memory_order_relaxed);
		_weatherWetness.store(weatherWetness.wetness, std::memory_order_relaxed);
		_wetness.store(wetness, std::memory_order_relaxed);
		return {
			.OcclusionViewProj = weather.occlusionViewProj,
			.Time = static_cast<float>(_rainTimer) / 1000.0f,
			.Raining = raining,
			.Wetness = wetness,
			.PuddleWetness = puddleWetness,
			.EnableWetnessEffects = _settings.enabled ? 1u : 0u,
			.MaxRainWetness = _settings.maxRainWetness,
			.MaxPuddleWetness = _settings.maxPuddleWetness,
			.MaxShoreWetness = _settings.enabled ? _settings.maxShoreWetness : 0.0f,
			.ShoreRange = _settings.shoreRange,
			.PuddleRadius = _settings.puddleRadius,
			.PuddleMaxAngle = _settings.puddleMaxAngle,
			.PuddleMinWetness = _settings.puddleMinWetness,
			.MinRainWetness = _settings.minRainWetness,
			.SkinWetness = _settings.skinWetness,
			.WeatherTransitionSpeed = _settings.weatherTransitionSpeed,
			.EnableRaindropFx = _settings.enableRaindropFx ? 1u : 0u,
			.EnableSplashes = _settings.enableSplashes ? 1u : 0u,
			.EnableRipples = _settings.enableRipples ? 1u : 0u,
			.EnableVanillaRipples = _settings.enableVanillaRipples ? 1u : 0u,
			.RaindropFxRange = _settings.raindropFxRange,
			.RaindropGridSizeRcp = 1.0f / _settings.raindropGridSize,
			.RaindropIntervalRcp = 1.0f / _settings.raindropInterval,
			.RaindropChance = _settings.raindropChance * raining * raining,
			.SplashesLifetime = _settings.splashesLifetime,
			.SplashesStrength = _settings.splashesStrength,
			.SplashesMinRadius = _settings.splashesMinRadius,
			.SplashesMaxRadius = _settings.splashesMaxRadius,
			.RippleStrength = _settings.rippleStrength,
			.RippleRadius = _settings.rippleRadius,
			.RippleBreadth = _settings.rippleBreadth,
			.RippleLifetimeRcp = _settings.raindropInterval / _settings.rippleLifetime,
			.pad0 = 0.0f
		};
	}

	void WetnessEffects::BindCompositeResources(ID3D11DeviceContext* a_context)
	{
		if (!a_context) {
			return;
		}
		// resolve per draw: target recreation moves its pool slot
		auto* srv =
			cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
		// a null bind reads outside the encode domain, which is wetness identity
		cs::engine::BindInjectionShaderResources(a_context, kGbufferNormalPSSlot, 1, &srv);
		if (srv) {
			_normalBinds.fetch_add(1, std::memory_order_relaxed);
		} else {
			_normalBindsNull.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void WetnessEffects::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto lightSnapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto compositeSnapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfComposite);
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("material_producer_ready", _filmReady.load(std::memory_order_relaxed))
			.Field("material_producer_draws", _producerDraws.load(std::memory_order_relaxed))
			.Field("material_producer_rejected", _producerRejected.load(std::memory_order_relaxed))
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("is_exterior", _isExterior.load(std::memory_order_relaxed))
			.Field(
				"wetness",
				static_cast<double>(_wetness.load(std::memory_order_relaxed)))
			.Field(
				"weather_wetness",
				static_cast<double>(_weatherWetness.load(std::memory_order_relaxed)))
			.Field(
				"max_rain_wetness",
				static_cast<double>(_settings.maxRainWetness))
			.Field(
				"min_rain_wetness",
				static_cast<double>(_settings.minRainWetness))
			.Field("puddle_radius", static_cast<double>(_settings.puddleRadius))
			.Field("puddle_max_angle", static_cast<double>(_settings.puddleMaxAngle))
			.Field("max_puddle_wetness", static_cast<double>(_settings.maxPuddleWetness))
			.Field("max_shore_wetness", static_cast<double>(_settings.maxShoreWetness))
			.Field("shore_range", static_cast<std::int64_t>(_settings.shoreRange))
			.Field(
				"normal_binds",
				static_cast<std::int64_t>(_normalBinds.load(std::memory_order_relaxed)))
			.Field(
				"normal_binds_null",
				static_cast<std::int64_t>(
					_normalBindsNull.load(std::memory_order_relaxed)))
			.Field(
				"light_matches",
				static_cast<std::int64_t>(lightSnapshot.matches))
			.Field(
				"light_substitutions",
				static_cast<std::int64_t>(lightSnapshot.substitutions))
			.Field(
				"composite_matches",
				static_cast<std::int64_t>(compositeSnapshot.matches))
			.Field(
				"composite_substitutions",
				static_cast<std::int64_t>(compositeSnapshot.substitutions));
	}

	void WetnessEffects::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		std::array<dmui::ChoiceOption<std::size_t>, wetness_math::kClimates.size()> choices;
		for (std::size_t index = 0; index < choices.size(); ++index)
			choices[index] = { index, wetness_math::kClimates[index].name, wetness_math::kClimates[index].name };
		const auto selection = dmui::DrawChoice<std::size_t>("wetness-climate", wetness_math::DetectClimate(_settings),
			std::span<const dmui::ChoiceOption<std::size_t>>{ choices }, "Custom", "Climate Preset");
		if (edit.Discrete(selection.changed) && *selection.selected != 0)
			wetness_math::ApplyClimate(_settings, *selection.selected);
		std::apply([&](const auto&... fields) {
			const auto draw = [&](const auto& field) {
				auto& value = _settings.*field.member;
				const std::string label = std::string(field.description) + "##" + std::string(field.key);
				if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, bool>)
					edit.Discrete(dmui::ui::Checkbox(label.c_str(), &value));
				else {
					auto range = wetness_math::kSchema.EditRange(field.member);
					if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, float>)
						if (field.member == &Settings::rippleLifetime)
							range.max = _settings.raindropInterval;
					edit.Continuous(dmui::ui::SliderScalar(label.c_str(), &value, &range.min, &range.max));
				}
			};
			(draw(fields), ...);
		},
			wetness_math::kSchema.fields);

		const bool operational = _injectionsOperational.load(std::memory_order_relaxed);
		if (operational && _settings.enabled) {
			dmui::ui::TextDisabled(
				"Weather wetness: %.2f | exterior: %s",
				_weatherWetness.load(std::memory_order_relaxed),
				_isExterior.load(std::memory_order_relaxed) ? "yes" : "no");
		} else if (operational) {
			dmui::ui::TextDisabled(
				"Disabled: publishing zero wetness (weather wetness %.2f).",
				_weatherWetness.load(std::memory_order_relaxed));
		} else {
			dmui::ui::TextDisabled(
				"Inactive: %s",
				_validationDetail.empty() ?
					"shader delivery path unavailable" :
					_validationDetail.c_str());
		}

		Menu::Get().DrawDebugViewSelector(*this);
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
