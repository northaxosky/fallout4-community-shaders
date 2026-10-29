#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "SkySyncSettings.h"
#include "World/Climate.h"

#include "RE/N/NVGodrays.h"
#include "RE/N/NiPoint3.h"
#include "RE/S/Sky.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWorldSpace.h"

#include <DirectXMath.h>

#include <atomic>
#include <cfloat>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace RE
{
	class Setting;
}

namespace cs::features
{
	// Syncs sun and moon lights, shadows and godrays with the sky bodies.
	class SkySync : public Feature
	{
	public:
		using Settings = sky_sync::Settings;

		static SkySync* GetSingleton();

		std::string_view GetName() const override { return "SkySync"; }
		std::string_view GetDisplayName() const override { return "Sky Sync"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Synchronizes godrays and shadows with the actual sun and moon positions in the sky.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnDataLoaded() override;
		void DrawSettings() override;
		bool HasDebugUI() const override { return true; }
		void DrawDebug() override;
		void RestoreDefaultSettings() override;

		// Game-thread hooks. After Sky::Update: steers lights, consumes transitions.
		void OnSkyUpdated(RE::Sky* a_sky);
		// After Sky::UpdateColors: rebuilds night colors, then dims sunlight.
		void OnSkyUpdateColors(RE::Sky* a_sky);
		// After Sky::GetGodraysSettings: scales or rebuilds the godrays.
		void OnGodraySettings(RE::Sky* a_sky, RE::NVGodrays::MediumSettings& a_medium, RE::NVGodrays::PostprocessSettings& a_postprocess);

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(telemetry::Sink& a_sink) const override;

	private:
		SkySync() = default;

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(sky_sync::kSchema, _settings); }

		enum class Caster : std::uint8_t
		{
			kSun,
			kMasser,
			kSecunda,
			kZenith  // no moon up: pseudo skylight, no direct light or godrays
		};

		// Caster-independent inputs the fader reads from its owner.
		struct FadeContext
		{
			bool sunBelowHorizon = false;
			float flashWeight = 0.0f;
			float minShadowElevation = 0.0f;
		};

		struct ShadowFader
		{
			RE::NiPoint3 currentDir = { 0.0f, 0.0f, 1.0f };
			RE::NiPoint3 startDir = { 0.0f, 0.0f, 1.0f };
			RE::NiPoint3 dimDir = { 0.0f, 0.0f, 1.0f };  // drives the horizon dim
			Caster target = Caster::kSun;
			Caster source = Caster::kSun;  // target before the current transition
			float fadeTimer = 0.0f;
			float immediateTransitionRemaining = 0.0f;
			bool transitioning = false;
			float intensityFactor = 1.0f;  // scales light and godrays mid-change
			float startIntensityFactor = 1.0f;
			float casterFade = 1.0f;  // fades light and godrays towards the zenith
			float startCasterFade = 1.0f;

			// Fraction of the current transition completed, 0 to 1.
			float Progress(float a_fadeDuration) const { return a_fadeDuration > 0.0f ? fadeTimer / a_fadeDuration : 1.0f; }

			void Update(const RE::Sky& a_sky, const FadeContext& a_context, RE::NiPoint3 a_dirs[], float a_intensities[], float a_fadeDuration, float a_fadeAdvance, bool a_immediateTransition);
			static void SetLighting(const RE::Sky& a_sky, const FadeContext& a_context, RE::NiPoint3 a_dir);
			static void SetDirection(RE::NiPoint3& a_dir, float a_headingRadians, float a_elevRadians);
			static void SetElevation(RE::NiPoint3& a_dir, float a_elevRadians);
			static void ClampDirection(RE::NiPoint3& a_dir, float a_minElevationDegrees);
			static float ComputeAlignmentFactor(const RE::NiPoint3& a_current, const RE::NiPoint3& a_target);
			void Reset();
		};

		static constexpr size_t kBodyCount = static_cast<size_t>(Caster::kZenith);  // sun and both moons

		// FO4: sun-arc constants are GMSTs; these are the image defaults.
		static constexpr float kDefaultSunExtremeX = 400.0f;
		static constexpr float kDefaultSunExtremeY = 25.0f;
		static constexpr float kDefaultSunAlphaTransTime = 2.0f;

		Settings _settings;

		const RE::Setting* _sunExtremeX = nullptr;
		const RE::Setting* _sunExtremeY = nullptr;
		const RE::Setting* _sunAlphaTransTime = nullptr;
		const RE::Setting* _weatherFlashDirectional = nullptr;

		RE::TESObjectCELL* _currentCell = nullptr;
		bool _currentCellInterior = false;
		std::atomic<const RE::TESWorldSpace*> _currentWorldspace{ nullptr };
		std::atomic_bool _worldspaceDisabled{ false };  // read by the game thread; the list is not
		bool _active = false;                           // the last Update steered the light
		static constexpr float kNoSkyRotation = FLT_MAX;
		float _currentSkyRotation = kNoSkyRotation;
		float _lastGameHour = -1.0f;
		bool _immediateTransitionReady = false;

		DirectX::XMFLOAT4 _colors[kBodyCount] = {};
		float _currentDim = 1.0f;
		float _moonlightFade = 0.0f;
		float _twilightCap = 0.0f;
		float _flashWeight = 0.0f;  // lightning's share of the directional light
		bool _sunBelowHorizon = false;
		ShadowFader _shadowFader;

		// Game-thread snapshot for telemetry.
		struct TelemetryState
		{
			std::atomic_bool active{ false };
			std::atomic<Caster> caster{ Caster::kSun };
			std::atomic_bool transitioning{ false };
			std::atomic<float> elevationDegrees{ 0.0f };
			std::atomic<float> headingDegrees{ 0.0f };
			std::atomic<float> dim{ 1.0f };
			std::atomic<float> skyRotationDegrees{ 0.0f };
			std::atomic_uint64_t updates{ 0 };
			std::atomic_uint64_t completedTransitions{ 0 };
		};
		TelemetryState _telemetry;

		mutable std::mutex _worldspaceMutex;

		void PreparePendingTransitions();
		bool Update(RE::Sky* a_sky);

		// Whether night colors replace the time-blended ones.
		bool CanRebuildNightColors(const RE::Sky& a_sky) const;
		void ApplyMoonlightColors(RE::Sky& a_sky);

		// Radians of a sky-local direction above the effective horizon.
		float GetHeightAboveHorizon(const RE::NiPoint3& a_dir) const;
		// 0 at the effective horizon, 1 at the fade elevation above it.
		float GetHorizonDim(float a_heightAboveHorizon) const;
		// Fraction of the moonlight fade the hours cover; 1 for zero duration.
		float GetMoonlightFadeProgress(float a_hours) const { return _settings.HorizonFadeHours > 0.0f ? a_hours / _settings.HorizonFadeHours : 1.0f; }
		// Ramps moonlight in while the sun is down, never above the cap.
		void UpdateMoonlightFade(float a_advanceHours, bool a_immediate);
		// Moonlight cap: 0 at handover, rising after sunset, falling to sunrise.
		float GetTwilightCap(const engine::climate::Timing& a_timing, float a_hour, float a_sunHeight) const;
		// Hour the sun crosses an elevation, rising or setting; may leave 0-24.
		float GetSunCrossingHour(const engine::climate::Timing& a_timing, float a_elevation, bool a_rising) const;
		// Below the minimum elevation, holds the direction at the crossing.
		void HoldLowSunDirection(const engine::climate::Timing& a_timing, float a_hour, const RE::NiPoint3& a_sunDir, RE::NiPoint3& a_shadowDir) const;
		// Sky-local sun direction at any hour; the night arc is below the horizon.
		RE::NiPoint3 GetSunDirectionAtHour(const engine::climate::Timing& a_timing, float a_hour) const;
		float GetSunAngle() const;
		void SetSkyRotation(const RE::Sky& a_sky, RE::TESObjectCELL* a_cell);
		// Whether the editor ID is in the disabled list.
		bool IsWorldspaceListed(const RE::TESWorldSpace* a_worldspace) const;
		// Recomputes the cached flag; the Locked form needs _worldspaceMutex held.
		void RefreshWorldspaceDisabledLocked();
		void RefreshWorldspaceDisabled();
		// Hours the active path crosses the horizon, as { sunrise, sunset }.
		std::pair<float, float> GetSunHorizonHours(const engine::climate::Timing& a_timing) const;
		// The flash Sky::UpdateColors adds to the light; 0 mid-transition.
		float GetDirectionalFlash(const RE::Sky& a_sky) const;

		void ProcessSun(const RE::Sky& a_sky, const RE::NiPoint3& a_sunDir, float a_sunHeight, RE::NiPoint3 a_dirs[]);
		// Fades the disc and glare in from the effective horizon.
		void FadeSunAtHorizon(const RE::Sky& a_sky, float a_sunHeight) const;
		void ProcessMoon(const RE::Sky& a_sky, Caster a_type, RE::NiPoint3 a_dirs[], float a_intensities[]);

		// Sky-local disc distance; the alternate path is nearer at the horizon.
		float GetSunDistance(const RE::Sky& a_sky) const;
		float GetSunExtremeX() const;
		float GetSunExtremeY() const;
		// Sun::Update's arc, with the night half below the horizon, not mirrored.
		RE::NiPoint3 CalculateVanillaSunDirection(float a_hour, float a_sunrise, float a_sunset) const;
		// Angle along the alternate path's arc, 0 at sunrise and pi at sunset.
		static float GetAlternateSunPhase(float a_hour, float a_sunrise, float a_sunset) { return DirectX::XM_PI * (a_hour - a_sunrise) / (a_sunset - a_sunrise); }
		// Unit direction on the alternate arc, tilted by the sun angle.
		static RE::NiPoint3 CalculateAlternateSunDirection(float a_hour, float a_sunrise, float a_sunset, float a_sunAngle);

		void PublishTelemetry(bool a_completedTransition);
	};
}
