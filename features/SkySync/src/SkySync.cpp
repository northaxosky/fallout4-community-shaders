#include "SkySync.h"

#include <DearModdingUI/Client.h>

#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/HookRegistry.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/CelestialTransition.h"
#include "World/Moon.h"
#include "World/SunPlacement.h"

#include "RE/B/BGSGodRays.h"
#include "RE/B/BSTimer.h"
#include "RE/B/BSTriShape.h"
#include "RE/C/Calendar.h"
#include "RE/M/Moon.h"
#include "RE/N/NiMatrix3.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Setting.h"
#include "RE/S/Sun.h"
#include "RE/T/TESClimate.h"
#include "RE/T/TESWeather.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <utility>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skysync");

		constexpr float kSunHorizonDistanceRatio = 280.0f / 400.0f;
		constexpr float kSecondsPerGameHour = 3600.0f;
		constexpr float kAlignmentFadeStartAngle = 2.0f;
		constexpr float kAlignmentFadeEndAngle = 15.0f;
		constexpr float kAlternateSunHorizonOffsetHours = 0.25f;
		constexpr float kHoursPerDay = 24.0f;
		constexpr int kSunCrossingSearchIterations = 16;
		constexpr float kSunDiscFadeStartElevation = -2.0f;  // vs effective horizon
		constexpr float kSunDiscFadeElevation = 2.0f;
		constexpr float kSunGlareScale = 1.0f / 255.0f;  // glare byte to alpha
		constexpr float kSouthernSunAngle = 90.0f - 35.0f;
		constexpr float kNorthernSunAngle = 90.0f + 35.0f;
		constexpr float kVanillaSunAngle = 90.0f + 5.0f;
		constexpr RE::NiPoint3 kZenithDirection{ 0.0f, 0.0f, 1.0f };

		bool IsValidDirection(const RE::NiPoint3& a_dir)
		{
			return a_dir.x != 0.0f || a_dir.y != 0.0f || a_dir.z != 0.0f;
		}

		// FO4: the worldspace union holds a temp-data offset for interiors.
		RE::TESWorldSpace* GetCellWorldspace(const RE::TESObjectCELL* a_cell)
		{
			return a_cell && !a_cell->IsInterior() ? a_cell->worldSpace : nullptr;
		}

		std::string_view GetEditorID(const RE::TESWorldSpace* a_worldspace)
		{
			return a_worldspace ? std::string_view(a_worldspace->editorID) : std::string_view{};
		}

		// Sky::GetGodraysSettings' per-slot sum; a missing form uses defaults.
		void AccumulateGodrays(const RE::BGSGodRays* a_form, float a_weight, RE::NVGodrays::MediumSettings& a_medium, RE::NVGodrays::PostprocessSettings& a_postprocess)
		{
			if (!a_form) {
				a_medium.airScatter += RE::NiPoint3{ 0.6f, 1.52f, 3.31f } * a_weight;
				a_medium.forwardScatter += RE::NiPoint3{ 2.0f, 2.0f, 2.0f } * a_weight;
				a_medium.backScatter += RE::NiPoint3{ 1.0f, 1.0f, 1.0f } * a_weight;
				a_medium.phaseForward += a_weight * 0.75f;
				a_postprocess.intensity += a_weight;
				return;
			}

			const auto& data = a_form->data;
			a_medium.airScatter += RE::NiPoint3{ 0.18f, 0.46f, 1.0f } * (a_weight * data.scatterAir);
			a_medium.forwardScatter += RE::NiPoint3{ data.colorFwd.r, data.colorFwd.g, data.colorFwd.b } * (a_weight * data.scatterFwd);
			a_medium.backScatter += RE::NiPoint3{ data.colorBack.r, data.colorBack.g, data.colorBack.b } * (a_weight * data.scatterBack);
			a_medium.phaseForward += a_weight * data.phaseFwd;
			a_medium.phaseBackward += a_weight * data.phaseBack;
			a_postprocess.intensity += a_weight * data.intensity;
		}

		struct Sky_Update
		{
			static void thunk(RE::Sky* a_sky, float a_delta)
			{
				func(a_sky, a_delta);
				SkySync::GetSingleton()->OnSkyUpdated(a_sky);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Sky_UpdateColors
		{
			static void thunk(RE::Sky* a_sky)
			{
				func(a_sky);
				SkySync::GetSingleton()->OnSkyUpdateColors(a_sky);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Sky_GetGodraysSettings
		{
			static void thunk(RE::Sky* a_sky, RE::NVGodrays::MediumSettings* a_medium, RE::NVGodrays::PostprocessSettings* a_postprocess)
			{
				func(a_sky, a_medium, a_postprocess);
				SkySync::GetSingleton()->OnGodraySettings(a_sky, *a_medium, *a_postprocess);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

	}

	SkySync* SkySync::GetSingleton()
	{
		static SkySync instance;
		return &instance;
	}

	bool SkySync::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(sky_sync::kSchema, a_config, candidate, a_error))
			return false;
		_settings = std::move(candidate);
		_liveSettings = settings::BindLiveSettings(sky_sync::kSchema, _settings, [this] { RefreshWorldspaceDisabled(); }, {}, {}, &_worldspaceMutex);
		return true;
	}

	bool SkySync::SaveSettings()
	{
		return settings::SaveDelta(sky_sync::kSchema, GetConfigKey(), _settings, *L);
	}

	void SkySync::RestoreDefaultSettings()
	{
		{
			std::scoped_lock lock(_worldspaceMutex);
			_settings = Settings{};
			RefreshWorldspaceDisabledLocked();
		}
		SaveSettings();
	}

	void SkySync::Load()
	{
		static bool updateInstalled = false;
		static bool colorsInstalled = false;
		static bool godraysInstalled = false;
		if (!engine::EnsureDetourInstalled<Sky_Update>(updateInstalled, RE::ID::Sky::Update, "Sky::Update") ||
			!engine::EnsureDetourInstalled<Sky_UpdateColors>(colorsInstalled, RE::ID::Sky::UpdateColors, "Sky::UpdateColors") ||
			!engine::EnsureDetourInstalled<Sky_GetGodraysSettings>(godraysInstalled, RE::ID::Sky::GetGodraysSettings, "Sky::GetGodraysSettings")) {
			FailLoad("Sky Sync sky hooks failed to install.");
			return;
		}
		engine::SetCelestialTransitionHandlerAvailable(true);
		L->info("Installed hooks");
	}

	void SkySync::OnDataLoaded()
	{
		engine::RegisterCelestialTransitionSources();
		if (const auto collection = RE::GameSettingCollection::GetSingleton()) {
			_sunExtremeX = collection->GetSetting("fSunXExtreme");
			_sunExtremeY = collection->GetSetting("fSunYExtreme");
			_sunAlphaTransTime = collection->GetSetting("fSunAlphaTransTime");
			_weatherFlashDirectional = collection->GetSetting("fWeatherFlashDirectional");
		}
	}

	void SkySync::OnSkyUpdated(RE::Sky* a_sky)
	{
		bool ran = false;
		FeatureManager::Get().RunRuntimeCallback(*this, "Sky::Update", [&] {
			PreparePendingTransitions();
			const bool completed = Update(a_sky);
			if (completed)
				engine::CompleteCelestialTransition();
			PublishTelemetry(completed);
			ran = true;
		});
		if (!ran) {
			// A quarantined feature must not strand requests or keep dimming
			_active = false;
			engine::SetCelestialTransitionHandlerAvailable(false);
		}
	}

	void SkySync::PublishTelemetry(bool a_completedTransition)
	{
		const auto& dir = _shadowFader.currentDir;
		_telemetry.active.store(_active, std::memory_order_relaxed);
		_telemetry.caster.store(_shadowFader.target, std::memory_order_relaxed);
		_telemetry.transitioning.store(_shadowFader.transitioning, std::memory_order_relaxed);
		_telemetry.elevationDegrees.store(DirectX::XMConvertToDegrees(std::asin(std::clamp(dir.z, -1.0f, 1.0f))), std::memory_order_relaxed);
		_telemetry.headingDegrees.store(DirectX::XMConvertToDegrees(std::atan2(dir.x, dir.y)), std::memory_order_relaxed);
		_telemetry.dim.store(_currentDim, std::memory_order_relaxed);
		_telemetry.skyRotationDegrees.store(_currentSkyRotation == kNoSkyRotation ? 0.0f : DirectX::XMConvertToDegrees(_currentSkyRotation), std::memory_order_relaxed);
		_telemetry.updates.fetch_add(1, std::memory_order_relaxed);
		if (a_completedTransition)
			_telemetry.completedTransitions.fetch_add(1, std::memory_order_relaxed);
	}

	void SkySync::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		static constexpr std::array<std::string_view, 4> kCasterNames{ "sun", "masser", "secunda", "zenith" };
		a_sink
			.Field("active", _telemetry.active.load(std::memory_order_relaxed))
			.Field("caster", kCasterNames[static_cast<size_t>(_telemetry.caster.load(std::memory_order_relaxed))])
			.Field("transitioning", _telemetry.transitioning.load(std::memory_order_relaxed))
			.Field("light_elevation_deg", static_cast<double>(_telemetry.elevationDegrees.load(std::memory_order_relaxed)))
			.Field("light_heading_deg", static_cast<double>(_telemetry.headingDegrees.load(std::memory_order_relaxed)))
			.Field("dim", static_cast<double>(_telemetry.dim.load(std::memory_order_relaxed)))
			.Field("sky_rotation_deg", static_cast<double>(_telemetry.skyRotationDegrees.load(std::memory_order_relaxed)))
			.Field("worldspace_disabled", _worldspaceDisabled.load(std::memory_order_relaxed))
			.Field("updates", _telemetry.updates.load(std::memory_order_relaxed))
			.Field("completed_transitions", _telemetry.completedTransitions.load(std::memory_order_relaxed));
	}

	void SkySync::OnSkyUpdateColors(RE::Sky* a_sky)
	{
		if (!IsEnabled() || !_active || !a_sky) {
			_flashWeight = 0.0f;
			return;
		}

		ApplyMoonlightColors(*a_sky);

		const float lightFactor = (_settings.DimSunlightUnderHorizon ? _shadowFader.intensityFactor * _currentDim : _shadowFader.intensityFactor) * _shadowFader.casterFade;
		auto& dirLight = a_sky->skyColor[RE::TESWeather::kSunlight];
		// Lightning strikes overhead, so only the light under the flash dims
		const float flash = GetDirectionalFlash(*a_sky);
		const RE::NiColor flashLight{ std::min(flash, dirLight.r), std::min(flash, dirLight.g), std::min(flash, dirLight.b) };
		dirLight = { (dirLight.r - flashLight.r) * lightFactor + flashLight.r,
			(dirLight.g - flashLight.g) * lightFactor + flashLight.g,
			(dirLight.b - flashLight.b) * lightFactor + flashLight.b };
		const float totalLight = dirLight.r + dirLight.g + dirLight.b;
		_flashWeight = totalLight > 0.0f ? (flashLight.r + flashLight.g + flashLight.b) / totalLight : 0.0f;
	}

	bool SkySync::CanRebuildNightColors(const RE::Sky& a_sky) const
	{
		// Only where vanilla lights from weather colors, not templates or rooms
		return IsEnabled() && _active && _sunBelowHorizon && a_sky.currentWeather && a_sky.mode.get() == RE::Sky::Mode::kFull &&
		       !a_sky.extLightingOverride && !a_sky.currentRoom && !a_sky.previousRoom;
	}

	void SkySync::ApplyMoonlightColors(RE::Sky& a_sky)
	{
		if (!CanRebuildNightColors(a_sky))
			return;

		// Vanilla's night blend outlasts the sun, so drop the time blend
		const float currentWeight = a_sky.currentWeatherPct;
		const float lastWeight = a_sky.lastWeather ? 1.0f - currentWeight : 0.0f;

		auto nightBlend = [&](std::size_t a_type) {
			auto nightColor = [a_type](const RE::TESWeather* a_weather) {
				return a_weather ? a_weather->colorData[a_type][static_cast<std::size_t>(RE::TESWeather::ColorTime::kNight)] : 0u;
			};
			return RE::Sky::COLOR_BLEND{ { nightColor(a_sky.currentWeather), 0u, nightColor(a_sky.lastWeather), 0u }, { currentWeight, 0.0f, lastWeight, 0.0f } };
		};

		// Full night at once as the light dims out; undimmed, keep vanilla's blend
		if (_settings.DimSunlightUnderHorizon)
			a_sky.SetColor(a_sky.skyColor[RE::TESWeather::kSunlight], nightBlend(RE::TESWeather::kSunlight), GetDirectionalFlash(a_sky));

		// Moon::Update tints the discs with this; ease it in with the moonlight
		RE::NiColor nightMoonGlare;
		a_sky.SetColor(nightMoonGlare, nightBlend(RE::TESWeather::kMoonGlare), 0.0f);
		auto& moonGlare = a_sky.skyColor[RE::TESWeather::kMoonGlare];
		moonGlare = { moonGlare.r + (nightMoonGlare.r - moonGlare.r) * _twilightCap,
			moonGlare.g + (nightMoonGlare.g - moonGlare.g) * _twilightCap,
			moonGlare.b + (nightMoonGlare.b - moonGlare.b) * _twilightCap };
	}

	void SkySync::OnGodraySettings(RE::Sky* a_sky, RE::NVGodrays::MediumSettings& a_medium, RE::NVGodrays::PostprocessSettings& a_postprocess)
	{
		if (!IsEnabled() || !_active || !a_sky)
			return;

		// FO4: godray medium and intensity stand in for volumetric lighting.
		if (_settings.DimVolumetricLighting && CanRebuildNightColors(*a_sky)) {
			const float currentWeight = a_sky->currentWeatherPct;
			const float lastWeight = 1.0f - currentWeight;
			a_medium = {};
			a_postprocess.intensity = 0.0f;
			constexpr auto night = static_cast<std::size_t>(RE::TESWeather::ColorTime::kNight);
			// The engine blends a missing weather in with its default godrays
			AccumulateGodrays(a_sky->currentWeather->godRays[night], currentWeight, a_medium, a_postprocess);
			AccumulateGodrays(a_sky->lastWeather ? a_sky->lastWeather->godRays[night] : nullptr, lastWeight, a_medium, a_postprocess);
		}

		a_postprocess.intensity *= (_settings.DimVolumetricLighting ? _shadowFader.intensityFactor * _currentDim : _shadowFader.intensityFactor) * _shadowFader.casterFade;
	}

	float SkySync::GetDirectionalFlash(const RE::Sky& a_sky) const
	{
		// Sky::SetColor caps the added flash at 1
		return !a_sky.lastWeather && _weatherFlashDirectional ? std::min(_weatherFlashDirectional->GetFloat() * a_sky.flash, 1.0f) : 0.0f;
	}

	void SkySync::PreparePendingTransitions()
	{
		const auto request = engine::ConsumeCelestialTransitionRequest();
		if (!request.timeJump && !request.gameLoad)
			return;

		if (request.gameLoad) {
			_shadowFader.Reset();
			_currentCell = nullptr;
			_currentCellInterior = false;
			_currentWorldspace.store(nullptr, std::memory_order_relaxed);
			RefreshWorldspaceDisabled();
			_currentSkyRotation = kNoSkyRotation;
		}

		_immediateTransitionReady = true;
	}

	bool SkySync::Update(RE::Sky* a_sky)
	{
		_active = false;
		_currentDim = 1.0f;

		if (!IsEnabled())
			return std::exchange(_immediateTransitionReady, false);

		if (!a_sky)
			return false;

		const auto sun = a_sky->sun;
		const auto climate = a_sky->currentClimate;
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!sun || !climate || !player)
			return false;

		const auto cell = player->GetParentCell();
		bool resetTransition = false;

		if (cell != _currentCell) {
			resetTransition = true;
			const auto prevCell = _currentCell;
			const bool resetFaderForCellChange = cell && prevCell &&
			                                     (cell->IsInterior() != _currentCellInterior ||
													 GetCellWorldspace(cell) != _currentWorldspace.load(std::memory_order_relaxed));
			if (cell)
				SetSkyRotation(*a_sky, cell);
			else
				_currentCell = nullptr;  // a cell-less frame must not reset every frame
			if (resetFaderForCellChange)
				_shadowFader.Reset();
		}

		// Exteriors run unless disabled or skyless; interiors need the sun shadow bits.
		const bool vanillaDrivesSun = _worldspaceDisabled.load(std::memory_order_relaxed) || a_sky->mode.get() == RE::Sky::Mode::kNone;
		if (vanillaDrivesSun || (cell && cell->IsInterior() && !cell->cellFlags.all(RE::TESObjectCELL::Flag::kInteriorSunShadowsA, RE::TESObjectCELL::Flag::kInteriorSunShadowsB))) {
			// Vanilla drives the sun here, so a pending transition has nothing to wait for
			return vanillaDrivesSun && std::exchange(_immediateTransitionReady, false);
		}
		_active = true;

		const float hour = a_sky->currentGameHour;
		const auto timing = engine::climate::GetTiming(*climate);

		RE::NiPoint3 directions[kBodyCount] = {};
		float intensities[kBodyCount] = {};

		const RE::NiPoint3 sunDir = GetSunDirectionAtHour(timing, hour);
		const float sunHeight = GetHeightAboveHorizon(sunDir);

		ProcessSun(*a_sky, sunDir, sunHeight, directions);
		ProcessMoon(*a_sky, Caster::kMasser, directions, intensities);
		ProcessMoon(*a_sky, Caster::kSecunda, directions, intensities);

		_sunBelowHorizon = sunHeight < 0.0f;
		// A sun below the horizon never casts, so skip the search
		if (!_sunBelowHorizon)
			HoldLowSunDirection(timing, hour, sunDir, directions[static_cast<int>(Caster::kSun)]);

		const auto* calendar = RE::Calendar::GetSingleton();
		const auto* timer = RE::BSTimer::GetSingleton();
		const float timeScale = calendar && calendar->timeScale ? calendar->timeScale->value : 0.0f;
		float fadeAdvance = timer ? std::max(timer->delta * timeScale, 0.0f) : 0.0f;

		// The clock can outrun real time or move while paused, so take the larger.
		if (_lastGameHour >= 0.0f) {
			const float hourDelta = std::remainder(hour - _lastGameHour, kHoursPerDay);  // shortest way round
			fadeAdvance = std::max(fadeAdvance, std::abs(hourDelta) * kSecondsPerGameHour);
		}
		_lastGameHour = hour;

		const bool transitionCompleted = std::exchange(_immediateTransitionReady, false);
		const FadeContext context{ _sunBelowHorizon, _flashWeight, _settings.MinShadowElevation };
		_shadowFader.Update(*a_sky, context, directions, intensities, _settings.ShadowTransitionDuration, fadeAdvance, transitionCompleted || resetTransition);

		_twilightCap = GetTwilightCap(timing, hour, sunHeight);
		UpdateMoonlightFade(fadeAdvance / kSecondsPerGameHour, transitionCompleted);
		// At night follow the light's direction, so the dim stays continuous
		_currentDim = _sunBelowHorizon ? _moonlightFade * GetHorizonDim(GetHeightAboveHorizon(_shadowFader.dimDir)) : GetHorizonDim(sunHeight);

		return transitionCompleted;
	}

	float SkySync::GetHeightAboveHorizon(const RE::NiPoint3& a_dir) const
	{
		return DirectX::XMScalarASinEst(a_dir.z) - DirectX::XMConvertToRadians(_settings.HorizonElevation);
	}

	float SkySync::GetHorizonDim(float a_heightAboveHorizon) const
	{
		// Elevation-based so every path fades over the same arc
		const float fadeRadians = std::max(DirectX::XMConvertToRadians(_settings.HorizonFadeElevation), FLT_EPSILON);
		return std::clamp(a_heightAboveHorizon / fadeRadians, 0.0f, 1.0f);
	}

	void SkySync::UpdateMoonlightFade(float a_advanceHours, bool a_immediate)
	{
		const float target = _sunBelowHorizon ? 1.0f : 0.0f;
		const float step = GetMoonlightFadeProgress(a_advanceHours);
		_moonlightFade = std::min(a_immediate ? target : std::clamp(target, _moonlightFade - step, _moonlightFade + step), _twilightCap);
	}

	float SkySync::GetTwilightCap(const engine::climate::Timing& a_timing, float a_hour, float a_sunHeight) const
	{
		if (a_sunHeight >= 0.0f)
			return 0.0f;

		// Derived from the clock, so scrubbing either way gives the same light
		const float sunriseHorizon = GetSunHorizonHours(a_timing).first;
		const float hoursToSunrise = a_hour < sunriseHorizon ? sunriseHorizon - a_hour : sunriseHorizon + kHoursPerDay - a_hour;
		float hoursSinceHandover = a_hour - GetSunCrossingHour(a_timing, DirectX::XMConvertToRadians(_settings.HorizonElevation), false);
		if (hoursSinceHandover < 0.0f)
			hoursSinceHandover += kHoursPerDay;
		if (hoursToSunrise < hoursSinceHandover)
			return GetHorizonDim(-a_sunHeight);
		return std::min(GetMoonlightFadeProgress(hoursSinceHandover), 1.0f);
	}

	float SkySync::GetSunCrossingHour(const engine::climate::Timing& a_timing, float a_elevation, bool a_rising) const
	{
		// Both paths rise to midday and sink back, so bisect within one half
		const auto [sunrise, sunset] = GetSunHorizonHours(a_timing);
		const float halfNight = (kHoursPerDay - (sunset - sunrise)) * 0.5f;
		float above = (sunrise + sunset) * 0.5f;
		float below = a_rising ? sunrise - halfNight : sunset + halfNight;
		for (int i = 0; i < kSunCrossingSearchIterations; ++i) {
			const float middle = (above + below) * 0.5f;
			(DirectX::XMScalarASinEst(GetSunDirectionAtHour(a_timing, middle).z) >= a_elevation ? above : below) = middle;
		}
		return below;
	}

	void SkySync::HoldLowSunDirection(const engine::climate::Timing& a_timing, float a_hour, const RE::NiPoint3& a_sunDir, RE::NiPoint3& a_shadowDir) const
	{
		const float minElevation = DirectX::XMConvertToRadians(_settings.MinShadowElevation);
		if (DirectX::XMScalarASinEst(a_sunDir.z) >= minElevation)
			return;

		// Hold where the sun crossed the minimum: no shadow drift while low
		const auto [sunrise, sunset] = GetSunHorizonHours(a_timing);
		const bool rising = a_hour < (sunrise + sunset) * 0.5f;
		a_shadowDir = GetSunDirectionAtHour(a_timing, GetSunCrossingHour(a_timing, minElevation, rising));
	}

	RE::NiPoint3 SkySync::GetSunDirectionAtHour(const engine::climate::Timing& a_timing, float a_hour) const
	{
		const auto [sunrise, sunset] = GetSunHorizonHours(a_timing);
		return _settings.UseAlternateSunPath ? CalculateAlternateSunDirection(a_hour, sunrise, sunset, GetSunAngle()) : CalculateVanillaSunDirection(a_hour, sunrise, sunset);
	}

	float SkySync::GetSunExtremeX() const
	{
		return std::abs(_sunExtremeX ? _sunExtremeX->GetFloat() : kDefaultSunExtremeX);
	}

	float SkySync::GetSunExtremeY() const
	{
		return _sunExtremeY ? _sunExtremeY->GetFloat() : kDefaultSunExtremeY;
	}

	RE::NiPoint3 SkySync::CalculateVanillaSunDirection(float a_hour, float a_sunrise, float a_sunset) const
	{
		// FO4: the arc's peak and offset come from fSunXExtreme/fSunYExtreme.
		const float peak = GetSunExtremeX();
		const float dayLength = a_sunset - a_sunrise;
		const float hoursSinceSunrise = a_hour >= a_sunrise ? a_hour - a_sunrise : a_hour + kHoursPerDay - a_sunrise;
		const bool day = hoursSinceSunrise < dayLength;
		const float x = day ? (1.0f - 2.0f * hoursSinceSunrise / dayLength) * peak :
		                      (2.0f * (hoursSinceSunrise - dayLength) / (kHoursPerDay - dayLength) - 1.0f) * peak;
		const float z = day ? peak - std::abs(x) : std::abs(x) - peak;
		RE::NiPoint3 dir{ x, GetSunExtremeY(), z };
		dir.Unitize();
		return dir;
	}

	RE::NiPoint3 SkySync::CalculateAlternateSunDirection(float a_hour, float a_sunrise, float a_sunset, float a_sunAngle)
	{
		float sinPhi, cosPhi, sinTilt, cosTilt;
		DirectX::XMScalarSinCosEst(&sinPhi, &cosPhi, GetAlternateSunPhase(a_hour, a_sunrise, a_sunset));
		DirectX::XMScalarSinCosEst(&sinTilt, &cosTilt, DirectX::XMConvertToRadians(a_sunAngle));

		RE::NiPoint3 dir{ cosPhi, -sinPhi * cosTilt, sinPhi * sinTilt };
		dir.Unitize();  // unit by construction, up to the estimates' error
		return dir;
	}

	float SkySync::GetSunDistance(const RE::Sky& a_sky) const
	{
		const float peak = GetSunExtremeX();
		if (_settings.UseAlternateSunPath) {
			const auto [sunrise, sunset] = GetSunHorizonHours(engine::climate::GetTiming(*a_sky.currentClimate));
			const float sinPhi = DirectX::XMScalarSinEst(GetAlternateSunPhase(a_sky.currentGameHour, sunrise, sunset));
			return std::lerp(peak * kSunHorizonDistanceRatio, peak, std::max(sinPhi, 0.0f));
		}

		const float distance = a_sky.sun->root->local.translate.Length();
		return distance < FLT_EPSILON ? peak : distance;
	}

	float SkySync::GetSunAngle() const
	{
		switch (_settings.SunPath) {
		case sky_sync::SunPathMode::kSouthern:
			return kSouthernSunAngle;
		case sky_sync::SunPathMode::kNorthern:
			return kNorthernSunAngle;
		case sky_sync::SunPathMode::kVanilla:
			return kVanillaSunAngle;
		case sky_sync::SunPathMode::kCustom:
			return 90.0f + _settings.CustomAngle;
		default:
			return kSouthernSunAngle;
		}
	}

	void SkySync::SetSkyRotation(const RE::Sky& a_sky, RE::TESObjectCELL* a_cell)
	{
		// An uninitialized interior has no north rotation yet; skip a frame
		if (a_cell->IsInterior() && a_cell->cellState == RE::TESObjectCELL::CELL_STATE::kNotLoaded)
			return;

		_currentCell = a_cell;
		_currentCellInterior = a_cell->IsInterior();
		_currentWorldspace.store(GetCellWorldspace(a_cell), std::memory_order_relaxed);
		RefreshWorldspaceDisabled();
		const float rotation = a_cell->GetNorthRotation();
		if (rotation == _currentSkyRotation)
			return;

		_currentSkyRotation = rotation;
		const float cosine = std::cos(-rotation);
		const float sine = std::sin(-rotation);
		// FO4: rotations are stored transposed (vectors map as local * rotation)
		a_sky.root->local.rotate = RE::NiMatrix3{
			cosine, -sine, 0.0f, 0.0f,
			sine, cosine, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f
		};
		RE::NiUpdateData updateData;
		a_sky.root->Update(updateData);
	}

	bool SkySync::IsWorldspaceListed(const RE::TESWorldSpace* a_worldspace) const
	{
		const auto editorID = GetEditorID(a_worldspace);
		// Editor IDs are case-insensitive in the engine
		return !editorID.empty() && std::ranges::any_of(_settings.DisabledWorldspaces, [editorID](const std::string& a_listed) { return util::EqualsIgnoreCase(a_listed, editorID); });
	}

	void SkySync::RefreshWorldspaceDisabledLocked()
	{
		_worldspaceDisabled.store(IsWorldspaceListed(_currentWorldspace.load(std::memory_order_relaxed)), std::memory_order_relaxed);
	}

	void SkySync::RefreshWorldspaceDisabled()
	{
		std::scoped_lock lock(_worldspaceMutex);
		RefreshWorldspaceDisabledLocked();
	}

	std::pair<float, float> SkySync::GetSunHorizonHours(const engine::climate::Timing& a_timing) const
	{
		// Vanilla's sun meets the horizon at the midpoint +- half fSunAlphaTransTime
		const float halfTransition = _settings.UseAlternateSunPath ? kAlternateSunHorizonOffsetHours :
		                                                             (_sunAlphaTransTime ? _sunAlphaTransTime->GetFloat() : kDefaultSunAlphaTransTime) * 0.5f;
		return { engine::climate::GetMiddleHour(a_timing.sunrise) - halfTransition, engine::climate::GetMiddleHour(a_timing.sunset) + halfTransition };
	}

	void SkySync::ProcessSun(const RE::Sky& a_sky, const RE::NiPoint3& a_sunDir, float a_sunHeight, RE::NiPoint3 a_dirs[])
	{
		// Replace vanilla's mirrored night arc, which a lowered horizon would reveal
		engine::SetSunPosition(a_sky, a_sunDir, GetSunDistance(a_sky));
		FadeSunAtHorizon(a_sky, a_sunHeight);

		a_dirs[static_cast<int>(Caster::kSun)] = a_sunDir;
	}

	void SkySync::FadeSunAtHorizon(const RE::Sky& a_sky, float a_sunHeight) const
	{
		const float fade = std::clamp((a_sunHeight - DirectX::XMConvertToRadians(kSunDiscFadeStartElevation)) / DirectX::XMConvertToRadians(kSunDiscFadeElevation), 0.0f, 1.0f);
		// Rebuilt as Sun::Update blends it; its alpha carries the clock-based fade
		const auto weatherGlare = [](const RE::TESWeather* a_weather, float a_weight) {
			return a_weather ? static_cast<std::uint8_t>(a_weather->weatherData[static_cast<std::size_t>(RE::TESWeather::WeatherData::kSunGlare)]) * kSunGlareScale * a_weight : 0.0f;
		};
		const float glare = weatherGlare(a_sky.currentWeather, a_sky.currentWeatherPct) + weatherGlare(a_sky.lastWeather, 1.0f - a_sky.currentWeatherPct);

		engine::SetSunAlpha(*a_sky.sun, fade, glare * fade);
	}

	void SkySync::ProcessMoon(const RE::Sky& a_sky, const Caster a_type, RE::NiPoint3 a_dirs[], float a_intensities[])
	{
		const int idx = static_cast<int>(a_type);
		_colors[idx] = {};

		const auto moon = a_type == Caster::kMasser ? a_sky.masser : a_sky.secunda;
		if (!moon || !moon->root || moon->root->GetAppCulled())
			return;

		a_dirs[idx] = engine::moon::GetDirection(*moon);

		const auto& baseColor = a_type == Caster::kMasser ? engine::moon::kMasserBaseColor : engine::moon::kSecundaBaseColor;
		const auto color = engine::moon::GetLightColor(a_sky, moon, baseColor, _settings.NewMoonIntensity, _settings.CrescentMoonIntensity, _settings.FullMoonIntensity).GetColor();
		_colors[idx] = color;

		const bool isValidSource = _settings.MoonLightSource == sky_sync::MoonSource::kBrightest ||
		                           (_settings.MoonLightSource == sky_sync::MoonSource::kMasser && a_type == Caster::kMasser) ||
		                           (_settings.MoonLightSource == sky_sync::MoonSource::kSecunda && a_type == Caster::kSecunda);
		if (!isValidSource)
			return;

		// A moon sinking toward the horizon loses out to one that is still up
		a_intensities[idx] = color.w * GetHorizonDim(GetHeightAboveHorizon(a_dirs[idx]));
	}

	void SkySync::ShadowFader::Reset()
	{
		target = source = Caster::kSun;
		fadeTimer = 0.0f;
		immediateTransitionRemaining = 0.0f;
		transitioning = false;
		intensityFactor = startIntensityFactor = 1.0f;
		casterFade = startCasterFade = 1.0f;
	}

	void SkySync::ShadowFader::Update(const RE::Sky& a_sky, const FadeContext& a_context, RE::NiPoint3 a_dirs[], float a_intensities[], float a_fadeDuration, float a_fadeAdvance, bool a_immediateTransition)
	{
		if (a_fadeDuration > 0.0f && a_immediateTransition)
			immediateTransitionRemaining = a_fadeDuration;
		else
			immediateTransitionRemaining = std::max(immediateTransitionRemaining - a_fadeAdvance, 0.0f);

		Caster best;

		if (a_context.sunBelowHorizon) {
			// An unlit moon must not hold off the zenith fallback
			auto isLit = [&](Caster a_moon) { return IsValidDirection(a_dirs[static_cast<int>(a_moon)]) && a_intensities[static_cast<int>(a_moon)] > 0.0f; };
			const bool masserValid = isLit(Caster::kMasser);
			const bool secundaValid = isLit(Caster::kSecunda);

			if (!masserValid && !secundaValid)
				best = Caster::kZenith;
			else if (!masserValid)
				best = Caster::kSecunda;
			else if (!secundaValid || a_intensities[static_cast<int>(Caster::kSecunda)] <= a_intensities[static_cast<int>(Caster::kMasser)])
				best = Caster::kMasser;
			else
				best = Caster::kSecunda;
		} else {
			best = Caster::kSun;
		}

		auto casterDir = [&](Caster a_caster) {
			return a_caster == Caster::kZenith ? kZenithDirection : a_dirs[static_cast<int>(a_caster)];
		};

		// If best source changed, begin a new transition
		if (best != target) {
			source = target;
			target = best;
			startDir = currentDir;
			startIntensityFactor = intensityFactor;
			startCasterFade = casterFade;
			fadeTimer = 0.0f;
			transitioning = true;
		}

		const RE::NiPoint3 targetDir = casterDir(target);
		const float targetCasterFade = target == Caster::kZenith ? 0.0f : 1.0f;

		if (!transitioning) {
			currentDir = dimDir = targetDir;
			intensityFactor = 1.0f;
			casterFade = targetCasterFade;
			immediateTransitionRemaining = 0.0f;
			SetLighting(a_sky, a_context, currentDir);
			return;
		}

		const float effectiveFadeAdvance = immediateTransitionRemaining > 0.0f ? a_fadeDuration : a_fadeAdvance;
		fadeTimer = std::min(fadeTimer + effectiveFadeAdvance, a_fadeDuration);
		const float t = Progress(a_fadeDuration);

		currentDir = {
			std::lerp(startDir.x, targetDir.x, t),
			std::lerp(startDir.y, targetDir.y, t),
			std::lerp(startDir.z, targetDir.z, t)
		};
		// Opposite directions cancel at the midpoint; pass through straight up
		if (currentDir.Unitize() <= FLT_EPSILON)
			currentDir = kZenithDirection;

		if (t >= 1.0f) {
			currentDir = targetDir;
			transitioning = false;
			immediateTransitionRemaining = 0.0f;
		}

		// Zenith is a stand-in: dim by the real caster; startDir covers a hidden one
		if (target == Caster::kZenith) {
			const RE::NiPoint3 sourceDir = casterDir(source);
			dimDir = IsValidDirection(sourceDir) ? sourceDir : startDir;
		} else {
			dimDir = source == Caster::kZenith ? targetDir : currentDir;
		}

		// Fade out leaving the old caster and back in reaching the new one
		const float fadeOut = startIntensityFactor * ComputeAlignmentFactor(currentDir, startDir);
		intensityFactor = std::max(fadeOut, ComputeAlignmentFactor(currentDir, targetDir));
		casterFade = std::lerp(startCasterFade, targetCasterFade, t);
		SetLighting(a_sky, a_context, currentDir);
	}

	void SkySync::ShadowFader::SetLighting(const RE::Sky& a_sky, const FadeContext& a_context, RE::NiPoint3 a_dir)
	{
		// Steer to the strike while the flash outshines the caster
		if (a_context.flashWeight > 0.0f) {
			a_dir = a_dir + (kZenithDirection - a_dir) * a_context.flashWeight;
			if (a_dir.Unitize() <= FLT_EPSILON)
				a_dir = kZenithDirection;
		}
		ClampDirection(a_dir, a_context.minShadowElevation);
		engine::SetSunLightDirection(a_sky, a_dir);
	}

	void SkySync::ShadowFader::SetDirection(RE::NiPoint3& a_dir, float a_headingRadians, float a_elevRadians)
	{
		float sinElev, cosElev, sinHeading, cosHeading;
		DirectX::XMScalarSinCosEst(&sinElev, &cosElev, a_elevRadians);
		DirectX::XMScalarSinCosEst(&sinHeading, &cosHeading, a_headingRadians);

		a_dir.x = cosElev * cosHeading;
		a_dir.y = cosElev * sinHeading;
		a_dir.z = sinElev;
	}

	void SkySync::ShadowFader::SetElevation(RE::NiPoint3& a_dir, float a_elevRadians)
	{
		SetDirection(a_dir, std::atan2(a_dir.y, a_dir.x), a_elevRadians);
	}

	float SkySync::ShadowFader::ComputeAlignmentFactor(const RE::NiPoint3& a_current, const RE::NiPoint3& a_target)
	{
		const float dot = std::clamp(a_current.Dot(a_target), -1.0f, 1.0f);
		const float angle = DirectX::XMConvertToDegrees(DirectX::XMScalarACosEst(dot));

		return std::clamp((kAlignmentFadeEndAngle - angle) / (kAlignmentFadeEndAngle - kAlignmentFadeStartAngle), 0.0f, 1.0f);
	}

	void SkySync::ShadowFader::ClampDirection(RE::NiPoint3& a_dir, float a_minElevationDegrees)
	{
		const float minElev = DirectX::XMConvertToRadians(a_minElevationDegrees);
		const float elev = DirectX::XMScalarASinEst(a_dir.z);
		if (elev >= minElev)
			return;

		SetElevation(a_dir, minElev);
	}

	void SkySync::DrawSettings()
	{
		static constexpr const char* sunPathNames[] = { "Southern Sky", "Northern Sky", "Vanilla", "Custom" };
		static constexpr const char* moonSourceNames[] = { "Brightest", "Masser", "Secunda" };

		settings::SettingsEdit edit{ *this };
		const auto toggle = [&](auto a_member, const char* a_tooltip) {
			const auto label = ui::FieldLabel(sky_sync::kSchema.Find(a_member));
			edit.Discrete(dmui::ui::Checkbox(label.c_str(), &(_settings.*a_member)));
			ui::Tooltip(a_tooltip);
		};
		const auto slider = [&](auto a_member, const char* a_format, const char* a_tooltip) {
			const auto label = ui::FieldLabel(sky_sync::kSchema.Find(a_member));
			const auto range = sky_sync::kSchema.EditRange(a_member);
			edit.Continuous(dmui::ui::SliderScalar(label.c_str(), &(_settings.*a_member), &range.min, &range.max, a_format, dmui::ui::SliderFlags::kAlwaysClamp));
			if (a_tooltip)
				ui::Tooltip(a_tooltip);
		};
		const auto choice = [&](auto a_member, const char* const* a_names, const char* a_tooltip) {
			const auto label = ui::FieldLabel(sky_sync::kSchema.Find(a_member));
			const auto range = sky_sync::kSchema.EditRange(a_member);
			auto value = static_cast<std::int32_t>(_settings.*a_member);
			const auto minimum = static_cast<std::int32_t>(range.min);
			const auto maximum = static_cast<std::int32_t>(range.max);
			if (edit.Continuous(dmui::ui::SliderScalar(label.c_str(), &value, &minimum, &maximum, a_names[value], dmui::ui::SliderFlags::kAlwaysClamp)))
				_settings.*a_member = static_cast<std::remove_cvref_t<decltype(_settings.*a_member)>>(value);
			ui::Tooltip(a_tooltip);
		};

		if (const ui::Section section{ "sky-sync-sun", "Sun" }; section) {
			toggle(&Settings::UseAlternateSunPath, "Calculate sun position based on time of day and season instead of vanilla movement.");
			if (_settings.UseAlternateSunPath) {
				choice(&Settings::SunPath, sunPathNames, "Choose the trajectory the sun takes across the sky.");
				if (_settings.SunPath == sky_sync::SunPathMode::kCustom)
					slider(&Settings::CustomAngle, "%.0f", "Set a custom angle for the sun's trajectory.");
			}
		}

		if (const ui::Section section{ "sky-sync-moons", "Moons" }; section) {
			choice(&Settings::MoonLightSource, moonSourceNames, "Select which moon casts shadows during the night.");
			slider(&Settings::NewMoonIntensity, "%.3f", nullptr);
			slider(&Settings::CrescentMoonIntensity, "%.3f", nullptr);
			slider(&Settings::FullMoonIntensity, "%.3f", nullptr);
		}

		if (const ui::Section section{ "sky-sync-shadows", "Shadows and Light" }; section) {
			slider(&Settings::MinShadowElevation, "%.1f deg", "The minimum angle sunlight will set to. Caps shadow length. Higher = shorter shadows at sunset/sunrise.");
			slider(&Settings::ShadowTransitionDuration, "%.0f", "How long (in game-time units) the shadow direction takes to fade between sources. 300 = ~15 seconds at timescale 20.");
			slider(&Settings::HorizonElevation, "%.1f deg", "Elevation treated as the horizon, where light fades to zero and the sun and moons hand over. Lower it to let the light fade out slightly below the visible horizon.");
			toggle(&Settings::DimSunlightUnderHorizon, "Fade directional light to zero as the sun or moon approaches the horizon.");
			toggle(&Settings::DimVolumetricLighting, "Also fade godrays with the directional dim around dawn and dusk.");
			if (_settings.DimSunlightUnderHorizon || _settings.DimVolumetricLighting) {
				slider(&Settings::HorizonFadeHours, "%.1f h", "How long (in game hours) moonlight takes to fade in once a moon casts shadows.");
				slider(&Settings::HorizonFadeElevation, "%.1f deg", "Height above the horizon at which the sun or moon light starts fading out. Higher = longer fade.");
			}
		}

		if (const ui::Section section{ "sky-sync-worldspaces", "Disabled Worldspaces", ui::Collapsible{ .count = _settings.DisabledWorldspaces.size() } }; section) {
			dmui::ui::TextDisabled("Sky Sync leaves lighting to vanilla in these worldspaces.");
			std::scoped_lock lock(_worldspaceMutex);
			bool listChanged = edit.Apply(ui::EditSetting("Worldspace Editor IDs", "DisabledWorldspaces", _settings.DisabledWorldspaces, settings::Range<settings::StringList>{}));

			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto cell = player ? player->GetParentCell() : nullptr;
			const std::string editorID(GetEditorID(GetCellWorldspace(cell)));
			const bool listed = IsWorldspaceListed(GetCellWorldspace(cell));
			dmui::ui::BeginDisabled(editorID.empty() || listed);
			if (edit.Discrete(dmui::ui::Button(("Add Current Worldspace (" + (editorID.empty() ? std::string("-") : editorID) + ")##addWorldspace").c_str()))) {
				_settings.DisabledWorldspaces.push_back(editorID);
				listChanged = true;
			}
			dmui::ui::EndDisabled();
			if (listChanged)
				RefreshWorldspaceDisabledLocked();
		}
	}

	void SkySync::DrawDebug()
	{
		static constexpr const char* casterNames[] = { "Sun", "Masser", "Secunda", "Zenith" };
		static constexpr const char* phaseNames[] = { "Full", "Waning Gibbous", "Waning Quarter", "Waning Crescent", "New", "Waxing Crescent", "Waxing Quarter", "Waxing Gibbous" };

		const auto* sky = RE::Sky::GetSingleton();
		const char* phase = sky ? phaseNames[static_cast<int>(engine::moon::GetPhase(*sky))] : "Unknown";
		const auto drawMoon = [&](const char* a_label, Caster a_caster) {
			const auto& color = _colors[static_cast<int>(a_caster)];
			dmui::ui::Text("%s  [%s]  color (%.3f, %.3f, %.3f, %.3f)", a_label, phase, static_cast<double>(color.x), static_cast<double>(color.y), static_cast<double>(color.z), static_cast<double>(color.w));
		};
		drawMoon("Masser", Caster::kMasser);
		drawMoon("Secunda", Caster::kSecunda);

		dmui::ui::Text("Dim: %.3f", static_cast<double>(_currentDim));
		dmui::ui::Separator();
		dmui::ui::Text("Shadow target: %s", casterNames[static_cast<int>(_shadowFader.target)]);
		dmui::ui::Text("Shadow dir:    (%.2f, %.2f, %.2f)", static_cast<double>(_shadowFader.currentDir.x), static_cast<double>(_shadowFader.currentDir.y), static_cast<double>(_shadowFader.currentDir.z));
		dmui::ui::Text("Transition intensity factor: %.3f", static_cast<double>(_shadowFader.intensityFactor));
		dmui::ui::Text("Caster fade: %.3f", static_cast<double>(_shadowFader.casterFade));
		if (_shadowFader.transitioning) {
			const float t = _shadowFader.Progress(_settings.ShadowTransitionDuration);
			dmui::ui::ProgressBar(t);
			dmui::ui::Text("Transitioning %.0f%%", static_cast<double>(t * 100.0f));
		} else {
			dmui::ui::TextDisabled("No transition");
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(SkySync::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
