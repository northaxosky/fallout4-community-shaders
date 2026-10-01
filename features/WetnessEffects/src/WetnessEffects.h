#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "Render/PixelShaderResourceSnapshot.h"
#include "Utils/CSBuffer.h"
#include "WetnessMath.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

struct ID3D11DeviceContext;

namespace cs::features
{
	class WetnessEffects : public Feature
	{
	public:
		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kWetnessTerm,
			kWorldUp
		};

		static WetnessEffects* GetSingleton();

		std::string_view GetName() const override { return "WetnessEffects"; }
		std::string_view GetDisplayName() const override { return "Wetness Effects"; }
		std::string GetConfigKey() const override { return "WetnessEffects"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override { return "Adds rain and shore wetness to deferred lighting and composition."; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;
		FullscreenDebugData GetFullscreenDebugData() const noexcept override;

		cs::WetnessEffectsFeatureData GetCommonBufferData() const;

		using Settings = wetness_math::Settings;

	private:
		WetnessEffects() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(wetness_math::kSchema); }
		void BindCompositeResources(ID3D11DeviceContext* a_context);
		void SaveCompositeBindings();
		void RestoreCompositeBindings();
		void BeginPrepass();
		void BindFilmOutput(ID3D11DeviceContext*);
		void BindFilmInput(ID3D11DeviceContext*, bool a_compute);

		static constexpr std::uint32_t kGbufferNormalPSSlot = 25;
		static constexpr std::array kCompositePSSlots{ kGbufferNormalPSSlot, 70u, 71u };

		Settings _settings;
		// every contribution and hook of the pair must register before any of them runs
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::atomic<DebugVisualization> _debugVisualization{
			DebugVisualization::kOff
		};
		std::string _validationDetail;

		mutable std::atomic_bool _isExterior{ false };
		// _wetness is the value published through b6; _weatherWetness is that value ungated
		mutable std::atomic<float> _wetness{ 0.0f };
		mutable std::atomic<float> _weatherWetness{ 0.0f };
		std::atomic_uint32_t _normalBinds{ 0 };
		std::atomic_uint32_t _normalBindsNull{ 0 };

		// render thread only
		std::array<cs::render::PixelShaderResourceSnapshot<1>, kCompositePSSlots.size()> _engineBindings;
		winrt::com_ptr<ID3D11Texture2D> _filmTexture;
		winrt::com_ptr<ID3D11RenderTargetView> _filmRTV;
		winrt::com_ptr<ID3D11ShaderResourceView> _filmSRV;
		winrt::com_ptr<ID3D11ShaderResourceView> _filmAvailabilitySRV;
		struct FilmBlend
		{
			winrt::com_ptr<ID3D11BlendState> native, film;
		};
		std::array<FilmBlend, D3D11_REQ_BLEND_OBJECT_COUNT_PER_DEVICE> _filmBlends;
		std::size_t _filmBlendCount = 0;
		std::atomic_bool _filmReady{ false };
		mutable std::atomic_bool _suppressRipples{ false };
		std::atomic_uint32_t _producerDraws{ 0 }, _producerRejected{ 0 };
		bool _inPrepass = false;
		mutable std::uint64_t _rainTimer = 0;
		mutable std::uint32_t _timerFrame = ~0u;
	};
}
