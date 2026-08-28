#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "Render/PixelShaderResourceSnapshot.h"
#include "WetnessMath.h"

#include <atomic>
#include <cstdint>
#include <string>

struct ID3D11DeviceContext;

namespace cs::features
{
	class WetnessEffects : public Feature
	{
	public:
		static WetnessEffects* GetSingleton();

		std::string_view GetName() const override { return "WetnessEffects"; }
		std::string_view GetDisplayName() const override { return "Wetness Effects"; }
		std::string GetConfigKey() const override { return "WetnessEffects"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override { return "Adds rain wetness to deferred lighting and composition."; }
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		cs::WetnessEffectsFeatureData GetCommonBufferData() const;

		using Settings = wetness_math::Settings;

	private:
		WetnessEffects() = default;

		void SaveSettings();
		void BindGbufferNormal(ID3D11DeviceContext* a_context);
		void SaveNormalBinding();
		void RestoreNormalBinding();

		static constexpr std::uint32_t kGbufferNormalPSSlot = 25;

		Settings _settings;
		// every contribution and hook of the pair must register before any of them runs
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::string _validationDetail;

		mutable std::atomic_bool _isExterior{ false };
		// _wetness is the value published through b6; _weatherWetness is that value ungated
		mutable std::atomic<float> _wetness{ 0.0f };
		mutable std::atomic<float> _weatherWetness{ 0.0f };
		std::atomic_uint32_t _normalBinds{ 0 };
		std::atomic_uint32_t _normalBindsNull{ 0 };

		// render thread only
		cs::render::PixelShaderResourceSnapshot<1> _engineNormalBinding;
	};
}
